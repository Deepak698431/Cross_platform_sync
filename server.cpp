#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN // <--- FIX 1: Silences the yellow terminal warnings!
#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>   
#include <ole2.h>
#include <objbase.h>  
#include <gdiplus.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>    
#include <chrono>     
#include "httplib.h"

using namespace Gdiplus;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

int main() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    httplib::Server svr;

    // ==============================================================================
    // ENDPOINT 1: PULL (Snipping Tool & Text)
    // ==============================================================================
    svr.Get("/pull", [](const httplib::Request& req, httplib::Response& res) {
        if (OpenClipboard(GetDesktopWindow())) {
            if (IsClipboardFormatAvailable(CF_BITMAP)) {
                HANDLE hData = GetClipboardData(CF_BITMAP);
                if (hData) {
                    Bitmap* bitmap = Bitmap::FromHBITMAP((HBITMAP)hData, NULL);
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        CLSID pngClsid;
                        CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &pngClsid);
                        IStream* pStream = NULL;
                        if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == 0) {
                            if (bitmap->Save(pStream, &pngClsid, NULL) == Gdiplus::Ok) {
                                HGLOBAL hGlobal = NULL;
                                GetHGlobalFromStream(pStream, &hGlobal);
                                if (hGlobal) {
                                    void* pData = GlobalLock(hGlobal);
                                    res.set_content(reinterpret_cast<const char*>(pData), GlobalSize(hGlobal), "image/png");
                                    GlobalUnlock(hGlobal);
                                }
                            }
                            pStream->Release();
                        }
                        delete bitmap;
                    }
                }
            } else if (IsClipboardFormatAvailable(CF_TEXT)) {
                HANDLE hData = GetClipboardData(CF_TEXT);
                if (hData) {
                    char* pszText = static_cast<char*>(GlobalLock(hData));
                    if (pszText) {
                        std::string text = pszText;
                        GlobalUnlock(hData);
                        res.set_content(text.c_str(), text.length(), "text/plain");
                    }
                }
            } else {
                res.set_content("No valid data.", "text/plain");
            }
            CloseClipboard();
        }
    });

    // ==============================================================================
    // ENDPOINT 2: PUSH TEXT
    // ==============================================================================
    svr.Post("/push", [](const httplib::Request& req, httplib::Response& res) {
        std::string new_text = req.body; 
        if (OpenClipboard(nullptr)) {
            EmptyClipboard(); 
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, new_text.length() + 1);
            if (hMem) {
                char* pMem = static_cast<char*>(GlobalLock(hMem));
                if (pMem) {
                    memcpy(pMem, new_text.c_str(), new_text.length() + 1);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_TEXT, hMem);
                }
            }
            CloseClipboard();
        }
        res.set_content("Success", "text/plain");
    });

    // ==============================================================================
    // ENDPOINT 3: PUSH IMAGE (Triple Format)
    // ==============================================================================
    svr.Post("/push_image", [](const httplib::Request& req, httplib::Response& res) {
        const std::string& image_data = req.body;
        if (image_data.empty()) {
            res.status = 400;
            res.set_content("Empty payload", "text/plain");
            return;
        }

        if (OpenClipboard(GetDesktopWindow())) {
            EmptyClipboard();

            // ----------------------------------------------------------------------
            // 1. Web PNG (Fallback for Chromium)
            // ----------------------------------------------------------------------
            UINT cfPNG = RegisterClipboardFormatA("PNG");
            HGLOBAL hPngMem = GlobalAlloc(GMEM_MOVEABLE, image_data.size());
            if (hPngMem) {
                memcpy(GlobalLock(hPngMem), image_data.data(), image_data.size());
                GlobalUnlock(hPngMem);
                SetClipboardData(cfPNG, hPngMem);
            }

            // ----------------------------------------------------------------------
            // 2. CF_HDROP (UNICODE FIX for Discord "File is empty" bug)
            // ----------------------------------------------------------------------
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            std::string finalPath = "C:\\YOUR_TARGET_DIRECTORY_HERE\\bridge_img_" + std::to_string(ms) + ".png";
            // std::string relativePath = "bridge_img_" + std::to_string(ms) + ".png"; 

            // char absolutePath[MAX_PATH];
            // GetFullPathNameA(relativePath.c_str(), MAX_PATH, absolutePath, NULL);
            // std::string finalPath = absolutePath;

            std::ofstream outfile(finalPath, std::ios::binary);
            if (outfile.is_open()) {
                outfile.write(image_data.data(), image_data.size());
                outfile.close();

                // Convert ANSI path to Wide String (Unicode)
                int wchars_num = MultiByteToWideChar(CP_UTF8, 0, finalPath.c_str(), -1, NULL, 0);
                std::vector<wchar_t> wPath(wchars_num);
                MultiByteToWideChar(CP_UTF8, 0, finalPath.c_str(), -1, &wPath[0], wchars_num);

                // Allocate memory for Unicode DROPFILES (+1 for double null terminator)
                SIZE_T dropSize = sizeof(DROPFILES) + (wchars_num + 1) * sizeof(wchar_t);
                HGLOBAL hDrop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dropSize); 
                if (hDrop) {
                    DROPFILES* pDrop = (DROPFILES*)GlobalLock(hDrop);
                    pDrop->pFiles = sizeof(DROPFILES);
                    pDrop->fWide = TRUE; // <--- FIX 2: Tells apps this is a Unicode string
                    memcpy((char*)pDrop + sizeof(DROPFILES), &wPath[0], wchars_num * sizeof(wchar_t));
                    GlobalUnlock(hDrop);
                    SetClipboardData(CF_HDROP, hDrop);
                }
            }

            // ----------------------------------------------------------------------
            // 3. CF_DIB (For Legacy) & CF_BITMAP (For Win+V Thumbnails)
            // ----------------------------------------------------------------------
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, image_data.size());
            if (hMem) {
                memcpy(GlobalLock(hMem), image_data.data(), image_data.size());
                GlobalUnlock(hMem);

                IStream* pStream = NULL;
                if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == 0) {
                    Bitmap* bitmap = Bitmap::FromStream(pStream);
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        
                        HBITMAP hBitmap;
                        // Strip alpha channel by rendering onto a solid white background
                        bitmap->GetHBITMAP(Color(255, 255, 255), &hBitmap);

                        HDC hdc = GetDC(NULL);
                        BITMAP bmp;
                        GetObject(hBitmap, sizeof(BITMAP), &bmp);

                        // Mathematically pure DIB header
                        BITMAPINFOHEADER bi = {0};
                        bi.biSize = sizeof(BITMAPINFOHEADER);
                        bi.biWidth = bmp.bmWidth;
                        bi.biHeight = bmp.bmHeight;
                        bi.biPlanes = 1;
                        bi.biBitCount = 24;
                        bi.biCompression = BI_RGB;
                        
                        DWORD stride = ((bmp.bmWidth * 24 + 31) / 32) * 4;
                        bi.biSizeImage = stride * bmp.bmHeight;

                        HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bi.biSizeImage);
                        if (hDIB) {
                            BITMAPINFOHEADER* pDIB = (BITMAPINFOHEADER*)GlobalLock(hDIB);
                            *pDIB = bi;
                            GetDIBits(hdc, hBitmap, 0, bmp.bmHeight, (BYTE*)pDIB + sizeof(BITMAPINFOHEADER), (BITMAPINFO*)pDIB, DIB_RGB_COLORS);
                            GlobalUnlock(hDIB);
                            SetClipboardData(CF_DIB, hDIB);
                        }
                        ReleaseDC(NULL, hdc);

                        // <--- FIX 3: Hand the lightweight handle directly to Win+V
                        SetClipboardData(CF_BITMAP, hBitmap); 
                        // Windows takes ownership of hBitmap, do not delete it!
                    }
                    if (bitmap) delete bitmap;
                    pStream->Release();
                }
            }

            CloseClipboard();
            std::cout << "Image successfully published to all clipboards!" << std::endl;
        }
        res.set_content("Image Success", "text/plain");
    });

    std::cout << "Clipboard Server running on port 8080..." << std::endl;
    svr.listen("0.0.0.0", 8080);
    
    Gdiplus::GdiplusShutdown(gdiplusToken); 
    return 0;
}
