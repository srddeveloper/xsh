# xsh

**xsh** is a lightweight developer utility CLI for **Linux** and **macOS** that provides a unified interface for common system, networking, and infrastructure tasks.

Instead of remembering dozens of different commands across many tools, **xsh** provides a simple command interface for everyday developer and sysadmin workflows.

---

## Features

- Simple command interface
- Network diagnostics tools
- Package management helpers
- System utilities
- Fast installation
- Minimal dependencies

---

## Installation

Install **xsh** with one command:

curl -fsSL https://raw.githubusercontent.com/SaudADS/xsh/main/xsh -o /usr/local/bin/xsh && chmod +x /usr/local/bin/xsh

Then run:

xsh help

---

## Commands

### Package Management

xsh install <package>
xsh update all
xsh setup all

Instead of juggling dozens of commands across different package managers and utilities, **xsh** simplifies common setup and update tasks into a few straightforward commands.

### Networking

xsh scan <ip>
xsh port <ip> <port>
xsh dns <host>
xsh whois <host>
xsh trace <host>

### Utility

xsh help

---

## Example Usage

Scan ports on a host:

xsh scan 192.168.1.1

Check DNS records:

xsh dns example.com

Trace a network route:

xsh trace example.com

---

## Philosophy

xsh follows a simple design philosophy:

- **fast**
- **minimal**
- **useful**
- **scriptable**

It aims to be a **Swiss-army knife CLI** for developers, homelabs, and system administrators.

---

## Contributing

xsh is open source and community contributions are welcome.

Ideas for contributions include:

- new commands
- platform support improvements
- performance optimizations
- bug fixes

If you'd like to contribute, open a pull request or issue.

---

## License

MIT License

Copyright (c) 2026 Saud Darwish
