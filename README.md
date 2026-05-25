# Local Cross-Platform Clipboard Bridge (iOS to Windows)

A lightweight, zero-cloud, high-performance background server built in C++ that enables real-time synchronization of text, URLs, and images from iOS devices directly into the native Windows OS Clipboard. 

This project bypasses modern cloud-sharing ecosystems by utilizing raw Win32 network sockets, direct kernel memory manipulation, and GDI+ graphics processing to synchronize clipboard payloads locally in milliseconds.

---

## 🚀 Key Features

- **Multi-Format Image Injection:** Simultaneously registers payloads as Web PNGs, Unicode File Drops (`CF_HDROP`), and Device-Independent Bitmaps (`CF_DIB`) for cross-application compatibility (Discord, Photoshop, File Explorer, MS Office).
- **Smart Data Routing:** Features dual-endpoint network processing (`/push` and `_image`) paired with a custom iOS Shortcut that filters text vs. binary streams dynamically.
- **Dynamic File Caching:** Automatically captures and logs files locally using high-precision timestamp generation (`<chrono>`), preventing race conditions and file overwrites.
- **Production Ready:** Compiles completely headless without an attached console footprint, running silently as a Windows background task.

---

## 🏗️ Architectural Overview
[ iPhone / iOS Device ]
│
▼ (Triggers via Back Tap or Share Sheet)
┌─────────────────────────────────┐
│     iOS "Traffic Cop" Logic     │ ──► Checks Type: Image vs. Text/URL
└─────────────────────────────────┘
│
├── [Text / URL Payload]  ──► POST to /push
└── [Raw Image Binary]    ──► POST to /push_image
│
▼ (Local Wi-Fi Network Route)
┌─────────────────────────────────┐
│      C++ HTTP Server Engine     │ ──► Powered by httplib.h (Port 8080)
└─────────────────────────────────┘
│
▼ (Direct Win32 API Interception)
┌─────────────────────────────────┐
│     Global Memory Allocation    │ ──► OpenClipboard() -> EmptyClipboard()
└─────────────────────────────────┘
│
├──► [Format 1] ──► Custom "PNG" Registration (For Web Browsers/Slack)
├──► [Format 2] ──► UNICODE CF_HDROP (Physical File Pointer for Desktop Paste)
└──► [Format 3] ──► Flattened 24-bit CF_DIB (Natively Renders Win+V Thumbnail)
---

## 🛠️ Systems Engineering Hurdles Resolved

### 1. The "Win + V" Blank Thumbnail Bug
* **The Challenge:** Windows Clipboard History (UWP Interface) frequently failed to draw previews of injected raw bitmaps, rendering a blank grey box instead. 
* **The Engineering Solution:** GDI+ stream data strips complex alpha channels onto a solid white canvas before cloning the image matrix to a strict 24-bit RGB pixel layout. By using a dry execution of `GetDIBits` with a null target buffer, the server forces the Windows Kernel to dynamically compute its exact required `biSizeImage` allocation profile. Passing this memory block straight to the `CF_DIB` stack ensures native UWP asset validation.

### 2. The Discord "File is Empty" Crash
* **The Challenge:** Pasting local file indicators (`CF_HDROP`) directly into Chromium-based applications like Discord resulted in immediate 0-byte structural reads.
* **The Engineering Solution:** Swapped traditional ANSI byte-string storage for an active conversion architecture using the `MultiByteToWideChar` API. Pointers are stored explicitly as Unicode Wide Strings (`wchar_t`) with `pDrop->fWide = TRUE` enabled inside the native `DROPFILES` struct, forcing multi-process sandboxes to correctly parse local relative environments.

---

## 💻 Compilation Guide

### Prerequisites
Ensure you are using a 64-bit MinGW environment or a `g++` compiler on Windows that supports C++11 or later.

### Standard Build (With Console Diagnostics Logs)
```bash
g++ server.cpp -o server.exe -std=c++11 -pthread -lws2_32 -lgdiplus -lole32
