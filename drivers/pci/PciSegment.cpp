#include <PciSegment.h>
#include <NameLookup.h>
#include <interfaces/driver/Devices.h>
#include <Log.h>
#include <VmObject.h>
#include <containers/Vector.h>
#include <NanoPrintf.h>
#include <Memory.h>

namespace Pci
{
    static void RawRw(void* addr, uintptr_t& data, size_t width, bool write)
    {
        sl::NativePtr ptr(addr);

        switch (width)
        {
        case 1:
            return write ? ptr.Write<uint8_t>((uint8_t)data) : (void)(data = ptr.Read<uint8_t>());
        case 2:
            return write ? ptr.Write<uint16_t>((uint16_t)data) : (void)(data = ptr.Read<uint16_t>());
        case 4:
            return write ? ptr.Write<uint32_t>((uint32_t)data) : (void)(data = ptr.Read<uint32_t>());
        }
    }

    bool BeginOp(npk_device_api* api, npk_iop_context* context, npk_iop_frame* iop_frame)
    {
        if (iop_frame->addr % 4 != 0)
            return false;
        if (iop_frame->length != 1 && iop_frame->length != 2 && iop_frame->length != 4)
            return false;
        if (context->op_type != Read && context->op_type != Write)
            return false;

        void* pciAddr = (void*)((uintptr_t)iop_frame->descriptor_data + iop_frame->addr);
        uintptr_t data = 0;
        sl::memcopy(iop_frame->buffer, &data, iop_frame->length);
        RawRw(pciAddr, data, iop_frame->length, context->op_type == npk_iop_type::Write);
        sl::memcopy(&data, iop_frame->buffer, iop_frame->length);

        return true;
    }

    bool EndOp(npk_device_api* api, npk_iop_context* context, npk_iop_frame* iop_frame)
    { return true; (void)api; (void)context; (void)iop_frame; } //no-op

    //4K per function, 8 functions per device, 32 devices per bus.
    constexpr size_t EcamBusWindowSize = 0x1000 * 8 * 32;
    constexpr const char NameFormatStr[] = "pci@%x::%02x:%02x:%01x (id=%04x:%04x c=%02x:%02x:%02x)";

    void* PciSegment::CalculateAddress(void* busAccess, uint8_t dev, uint8_t func)
    {
        return sl::NativePtr(busAccess).Offset((dev << 15) | (func << 12)).ptr;
    }

    void PciSegment::RegisterDescriptor(void* addr, uint8_t bus, uint8_t dev, uint8_t func)
    {
        const uint32_t vendor = ReadReg(addr, 0);
        const uint32_t type = ReadReg(addr, 2);

        npk_init_tag_pci_function* initTag = new npk_init_tag_pci_function();
        initTag->header.type = npk_init_tag_type::PciFunction;
        initTag->segment = id;
        initTag->bus = bus;
        initTag->device = dev;
        initTag->function = func;

        npk_load_name* names = new npk_load_name[2];
        names[0].type = npk_load_type::PciClass;
        names[0].length = 3;
        uint8_t* buffClass = new uint8_t[3];
        buffClass[0] = type >> 24;
        buffClass[1] = type >> 16;
        buffClass[2] = type >> 8;
        names[0].str = buffClass;

        names[1].type = npk_load_type::PciId;
        names[1].length = 4;
        uint8_t* buffId = new uint8_t[4];
        buffId[0] = vendor;
        buffId[1] = vendor >> 8;
        buffId[2] = vendor >> 16;
        buffId[3] = vendor >> 24;
        names[1].str = buffId;

        npk_device_desc* descriptor = new npk_device_desc();
        descriptor->init_data = &initTag->header;
        descriptor->friendly_name = { .length = 0, .data = nullptr };
        descriptor->load_name_count = 2;
        descriptor->load_names = names;
        descriptor->driver_data = addr;

        sl::String name = PciClassToName(vendor, type);
        if (name.IsEmpty())
        {
            const size_t nameLen = npf_snprintf(nullptr, 0, NameFormatStr, id, bus,
                dev, func, 0, 0, 0, 0, 0);
            char* nameBuff = new char[nameLen + 1];
            npf_snprintf(nameBuff, nameLen + 1, NameFormatStr, id, bus, dev, func,
                vendor & 0xFFFF, vendor >> 16, type >> 24, (type >> 16) & 0xFF, (type >> 8) & 0xFF);

            descriptor->friendly_name.length = nameLen;
            descriptor->friendly_name.data = nameBuff;
        }
        else
        {
            descriptor->friendly_name.length = name.Size();
            descriptor->friendly_name.data = name.DetachBuffer();
        }

        VALIDATE_(npk_add_device_desc(descriptor, true) != NPK_INVALID_HANDLE, );
        (void)descriptor;
    }

    void PciSegment::WriteReg(void* addr, size_t reg, uint32_t value)
    {
        sl::NativePtr(addr).Offset(reg * 4).Write<uint32_t>(value);
    }

    uint32_t PciSegment::ReadReg(void* addr, size_t reg)
    {
        return sl::NativePtr(addr).Offset(reg * 4).Read<uint32_t>();
    }

    bool PciSegment::RawAccess(size_t width, uintptr_t addr, uintptr_t* data, bool write)
    {
        addr &= 0xFFFF'FFFF;
        const uint8_t bus = (addr >> 20) & 0xFF;
        const uint8_t dev = (addr >> 15) & 0x1F;
        const uint8_t func = (addr >> 12) & 0x7;

        for (size_t i = 0; i < busAccess.Size(); i++)
        {
            if (busAccess[i].busId != bus)
                continue;

            RawRw(CalculateAddress(busAccess[i].access->ptr, dev, func), *data, width, write);
            return true;
        }

        return false;
    }

    bool PciSegment::Init(const npk_init_tag_pci_host* host)
    {
        VALIDATE(host->type == npk_pci_host_type::Ecam, false, "PCI host type not supported");
        id = host->id;
        base = host->base_addr;
        //TODO: add support for x86 port io

        //create IO device API for the kernel, this will be the 'transport api' for PCI devices we create.
        ioApi.header.type = npk_device_api_type::Io;
        ioApi.header.driver_data = this;
        ioApi.begin_op = BeginOp;
        ioApi.end_op = EndOp;
        ioApi.header.get_summary = nullptr;
        VALIDATE_(npk_add_device_api(&ioApi.header), false);
        VALIDATE_(npk_set_transport_api(ioApi.header.id), false);

        sl::Vector<uint8_t> busses {};
        busses.PushBack(host->first_bus);

        while (!busses.Empty())
        {
            const uint8_t bus = busses.PopBack();
            auto& busVmo = busAccess.EmplaceBack();
            busVmo.busId = bus;
            const uintptr_t physAddr = host->base_addr + (bus << 20);
            busVmo.access = dl::VmObject(EcamBusWindowSize, physAddr, dl::VmFlag::Mmio | dl::VmFlag::Write);

            for (uint8_t dev = 0; dev < 32; dev++)
            {
                void* const devAddr = CalculateAddress(busVmo.access->ptr, dev, 0);
                const uint32_t reg0 = ReadReg(devAddr, 0);
                if ((reg0 & 0xFFFF) == 0xFFFF)
                    continue;

                const uint32_t reg3 = ReadReg(devAddr, 3);
                const uint8_t headerType = (reg3 >> 16) & 0x7F;
                if (headerType == 1)
                {
                    //TODO: check if pci-pci bridge has been configured by firmware, or if we need to init it.
                    const uint32_t reg6 = ReadReg(devAddr, 6);
                    busses.PushBack((reg6 >> 8) & 0xFF);
                    continue;
                }
                else if (headerType != 0)
                {
                    Log("Unknown header type %hu (%02x:%02x:00)", LogLevel::Error,
                        headerType, bus, dev);
                    continue;
                }

                const size_t funcCount = ((reg3 >> 16) & 0x80) != 0 ? 8 : 1;
                for (size_t func = 0; func < funcCount; func++)
                {
                    void* const funcAddr = CalculateAddress(busVmo.access->ptr, dev, func);
                    if ((ReadReg(funcAddr, 0) & 0xFFFF) == 0xFFFF)
                        continue;
                    RegisterDescriptor(funcAddr, bus, dev, func);
                }
            }
        }

        return true;
    }
}
