# NP Window Server

## Overview
This document talks about how to communicate with the window server, as well as some of the server internals.
The window server runs in user mode, but does access the main framebuffers and input devices directly.
The window server is designed to abstract over interacting with devices directly: a window client is provided with an appropriate framebuffer,
and is forwarded input from the user if focused or certain flags are negotiated.

## np-gui
`np-gui` is one of the libraries included with northport and it serves two main purposes. 
The first is providing a gui framework as you might expected, the other is a nice interface for interacting with the window server.
This is the suggested way of dealing with the window server, but if you wanted to use `np-gui` you wouldn't be reading this I guess.

## Protocols
The window server is designed to operate over a number of protocols. For now only IPC mailboxes are supported, but the intent is to add support for TCP once a network stack is implemented in the kernel, and possibly a shell-over-the-network setup like is possible with ssh and X forwarding. Each protocol carries the same set of request and response packets with an optional protocol-specific header prefixed.

Data from the client (your application) to the server is called a request packet, and data being sent from the server is called a response. All requests will generate a response (note some responses will simply be an acknowledged-message), but not every response has a associated request. The server may send a 'response' message that has no request. This can be the result of some user input, or another system event.

### IPC Mailbox
Currently the only backend to the window server. Messages being sent to the window server as sent to the IPC mailbox "window-server/incoming". The `CreateClient` message has a return field that provides the mailbox address the server can use to return data.

## Protocol Messages (Request)

## Protocol Messages (Response)

## Structure of The Window Server
The window server can be broken down into three core parts:
- Rendering: Self explanatory, it manages graphics resources required for using the various window framebuffers, and renders them to the main framebuffer with the requested decorations. It uses a few techniques to speeed up rendering, by only clearing and then re-rendering what has changed.
- Protocols: Here is where the various protocol backends live, and the generic protocol that provides an interface over all of them.
- Window Management: As you'd expect, this section represents the windows as 'descriptors'. Each window descriptor maintains various details about the window's position and size, as well as any status or control flags. These are manipulated by the protocols layer, and are provided to the rendering layer when needed. Other than the window descriptors, the WM owns no resources, instead only containing handles to other parts. It's really the glue that ties the other parts together.
