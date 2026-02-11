#include <iostream>
#include <string>
#include <cpr/cpr.h>
#include <format>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

void Error(std::string Message = "Unknown error.") {
    std::cerr << "Fatal error has encountered: " << Message << std::endl;
    std::exit(EXIT_FAILURE);
}

std::string GetPrinterURL() {
    std::atomic<bool> Found(false);
    std::string Result;
    std::mutex ResultMutex;
    std::vector<std::thread> Threads;
    auto ScanRange = [&](int Start, int End) {
        for (int IPIndex = Start; IPIndex <= End && !Found.load(); IPIndex++) {
            cpr::Response r = cpr::Get(
                cpr::Url{std::format("http://10.0.0.{}", IPIndex)},
                cpr::Timeout{5000},
                cpr::Redirect{true}
            );
            std::string HTML = r.text;
            if (r.status_code == 200 && HTML.contains("Creality")) {
                std::lock_guard<std::mutex> Lock(ResultMutex);
                Result = std::format("10.0.0.{}", IPIndex);
                Found.store(true);
                return;
            }
        }
    };
    unsigned int cores = std::thread::hardware_concurrency();
    int NumberThreads = 100;
    int ChunkSize = 254 / NumberThreads;
    for (int i = 0; i < NumberThreads; i++) {
        int start = i * ChunkSize + 1;
        int end = (i == NumberThreads - 1) ? 254 : start + ChunkSize - 1;
        Threads.emplace_back(ScanRange, start, end);
    }
    for (auto& t : Threads) {
        if (t.joinable()) t.join();
    }
    return Result;
}

bool UploadFileToPrinter(std::string FilePath, std::string URL) {
    fs::path FileInfo(FilePath);
    cpr::Multipart Multipart{
        cpr::Part{"file", cpr::File{FilePath}},
        cpr::Part{"filename", std::filesystem::path(FilePath).filename().string()},
        cpr::Part{"root", "gcodes"},
        cpr::Part{"print", "false"}
    };
    cpr::Response r = cpr::Post(
        cpr::Url{std::format("http://{}:7125/server/files/upload", URL)},
        Multipart,
        cpr::Header{{"User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:147.0) Gecko/20100101 Firefox/147.0"}},
        cpr::Timeout{60000});
    std::string Text = r.text;
    return r.status_code >= 200 && r.status_code < 300 && !Text.empty() && Text.contains("\"item\"");
}


int main(int argc, char* argv[]) {
    std::string URL;
    std::string FilePath;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--url" || std::string(argv[i]) == "--u") {
            URL = argv[++i];
        } else if (std::string(argv[i]) == "--file" || std::string(argv[i]) == "--f") {
            FilePath = argv[++i];
        }
    }
    if (URL.empty()) {
        std::cout << "Looking for printer URL." << '\n';
        URL = GetPrinterURL();
        if (URL.empty()) {
            Error("Could not find printer! Please make sure you are connected to the same network as the printer & it is powered on.");
        }
        std::cout << "Found printer URL." << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!FilePath.empty()) {
        if (!fs::exists(FilePath)) {
            Error("File does not exist!");
        }
        std::cout << "Uploading file to the printer." << '\n';
        if (UploadFileToPrinter(FilePath, URL)) {
            std::cout << "Uploaded file to the printer." << '\n';
        } else {
            Error("Could not upload the file to the printer!");
        }
    } else {
        Error("Please use the --file /path/to/file/ flag to input a file.");
    }
    return 0;
}
