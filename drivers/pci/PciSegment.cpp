#include <PciSegment.h>
#include <NameLookup.h>
#include <interfaces/driver/Devices.h>
#include <Log.h>
#include <VmObject.h>
#include <containers/Vector.h>
#include <NanoPrintf.h>

namespace Pci
{
    bool BeginOp(npk_device_api* api, npk_iop_context* context, npk_iop_frame* iop_frame)
    {
        auto segment = static_cast<PciSegment*>(api->driver_data);
        if (iop_frame->addr % 4 != 0)
            return false;
        if (iop_frame->length != 4)
            return false;
        //TODO: support variable-sized and misaligned operations, a base-level driver like this should be flexible!

        sl::NativePtr buffPtr = iop_frame->buffer;
        switch (context->op_type)
        {
        case npk_iop_type::Read:
            {
                const uint32_t reg = segment->ReadReg(iop_frame->descriptor_data, iop_frame->addr / 4);
                buffPtr.Write(reg);
                break;
            }
        case npk_iop_type::Write:
            {
                segment->WriteReg(iop_frame->descriptor_data, iop_frame->addr / 4, buffPtr.Read<uint32_t>());
                break;
            }
            break;
        default:
            return false;
        }

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
