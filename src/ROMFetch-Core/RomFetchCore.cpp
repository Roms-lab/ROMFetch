#include "RomFetchCore.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string_view>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#include <Windows.h>
#endif
#include <cpr/cpr.h>

namespace fs = std::filesystem;

namespace {

#define ROMFETCH_RAW_BASE "https://raw.githubusercontent.com/Roms-lab/ROMFetch/refs/heads/main/"
constexpr const char* kRomListUrl = ROMFETCH_RAW_BASE "metadata/RomList.txt";
constexpr const char* kVersionUrl = ROMFETCH_RAW_BASE "metadata/Version.txt";
constexpr const char* kUpdateBatUrl = ROMFETCH_RAW_BASE "Update/Update.bat";

int HexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return 10 + c - 'A';
	}
	if (c >= 'a' && c <= 'f') {
		return 10 + c - 'a';
	}
	return -1;
}

std::string UrlDecode(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size()) {
			const int hi = HexValue(static_cast<char>(s[i + 1]));
			const int lo = HexValue(static_cast<char>(s[i + 2]));
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		out.push_back(static_cast<char>(s[i]));
	}
	return out;
}

std::string FilenameFromUrl(const char* url) {
	const char* last = std::strrchr(url, '/');
	if (!last || !last[1]) {
		return "download.bin";
	}
	return UrlDecode(std::string_view(last + 1));
}

bool HttpOk(const cpr::Response& r) {
	return r.error.code == cpr::ErrorCode::OK && r.status_code == cpr::status::HTTP_OK;
}

cpr::Header HttpNoCacheHeaders() {
	return cpr::Header{
	    {"Cache-Control", "no-cache, no-store, must-revalidate"},
	    {"Pragma", "no-cache"},
	};
}

void TrimVersionString(std::string& s) {
	while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
		s.pop_back();
	}
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.erase(s.begin());
	}
	if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB
	    && static_cast<unsigned char>(s[2]) == 0xBF) {
		s.erase(0, 3);
	}
}

void EmitLine(const romfetch::ProgressSink& sink, const char* fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (sink) {
		sink(std::string(buf));
	}
	else {
		std::fputs(buf, stdout);
		std::fflush(stdout);
	}
}

cpr::ProgressCallback MakeProgressCallback(std::string label, romfetch::ProgressSink sink) {
	auto lastPrint = std::make_shared<std::chrono::steady_clock::time_point>();
	auto havePrinted = std::make_shared<bool>(false);
	return cpr::ProgressCallback{[label = std::move(label), lastPrint, havePrinted, sink](
	                                 cpr::cpr_pf_arg_t dltotal, cpr::cpr_pf_arg_t dlnow, cpr::cpr_pf_arg_t ultotal,
	                                 cpr::cpr_pf_arg_t ulnow, intptr_t) -> bool {
		(void)ultotal;
		(void)ulnow;
		const auto now = std::chrono::steady_clock::now();
		const bool complete = (dltotal > 0 && dlnow >= dltotal);
		if (!complete && *havePrinted) {
			if (now - *lastPrint < std::chrono::milliseconds(120)) {
				return true;
			}
		}
		*havePrinted = true;
		*lastPrint = now;

		if (dltotal > 0) {
			const double pct = 100.0 * static_cast<double>(dlnow) / static_cast<double>(dltotal);
			EmitLine(sink, "\r  [%s]  %lld / %lld bytes (%.1f%%)   ", label.c_str(), static_cast<long long>(dlnow),
			         static_cast<long long>(dltotal), pct);
		}
		else if (dlnow > 0) {
			EmitLine(sink, "\r  [%s]  %lld bytes (total size unknown)...   ", label.c_str(),
			         static_cast<long long>(dlnow));
		}
		else {
			EmitLine(sink, "\r  [%s]  connecting...   ", label.c_str());
		}
		return true;
	}};
}

void RemoveTreeIfExists(const fs::path& p) {
	std::error_code ec;
	fs::remove_all(p, ec);
}

} // namespace

namespace romfetch {

const char* AppVersion() {
	return "0.0.3";
}

#if defined(_MSC_VER)
void EnsureDllSearchPath() {
	wchar_t buf[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		return;
	}
	std::wstring p(buf, buf + n);
	const size_t slash = p.find_last_of(L'\\/');
	if (slash == std::wstring::npos) {
		return;
	}
	const std::wstring dllDir = p.substr(0, slash + 1) + L"DLL";
	SetDllDirectoryW(dllDir.c_str());
}
#endif // _MSC_VER

bool SaveTextFromUrl(const char* url, const fs::path& path, const char* progress_label, ProgressSink sink) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::string label = progress_label ? std::string(progress_label) : path.filename().string();
	cpr::Response r = cpr::Get(cpr::Url{url}, HttpNoCacheHeaders(), MakeProgressCallback(std::move(label), sink));
	if (!sink) {
		printf("\n");
	}
	else {
		sink("\n");
	}
	if (!HttpOk(r)) {
		return false;
	}
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	out << r.text;
	return true;
}

bool SaveBinaryFromUrl(const char* url, const fs::path& path, const char* progress_label, ProgressSink sink) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	const std::string label = progress_label ? std::string(progress_label) : path.filename().string();
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	cpr::Response r = cpr::Download(out, cpr::Url{url}, HttpNoCacheHeaders(), MakeProgressCallback(label, sink));
	if (!sink) {
		printf("\n");
	}
	else {
		sink("\n");
	}
	return HttpOk(r);
}

void Setup(ProgressSink sink) {
	RemoveTreeIfExists("Assets");
	std::error_code ec;
	fs::create_directories("Roms", ec);
	fs::create_directories("Assets", ec);
	SaveTextFromUrl(kRomListUrl, "Assets/RomList.txt", "Rom list", sink);
}

void CheckForUpdates(ProgressSink sink) {
	std::string LatestVersion;
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	    std::chrono::system_clock::now().time_since_epoch()).count();
	const std::string version_url = std::string(kVersionUrl) + "?_=" + std::to_string(ms);
	SaveTextFromUrl(version_url.c_str(), "Assets/Version.txt", "Version", sink);
	std::ifstream VersionFile("Assets/Version.txt");
	if (!VersionFile.is_open()) {
		if (!sink) {
			printf("Error, cannot check updates.");
		}
		else {
			sink("Error, cannot check updates.");
		}
		return;
	}
	std::getline(VersionFile, LatestVersion);
	VersionFile.close();
	TrimVersionString(LatestVersion);
	if (std::string(AppVersion()) != LatestVersion) {
#if defined(_WIN32)
		MessageBoxA(nullptr, "New version is available, you should update to the latest version for the best experience.",
		            "ROMFetch", MB_OK | MB_ICONINFORMATION);
#endif
	}
}

bool TryDownloadRom(const std::string& userSelection, ProgressSink sink) {
	const auto dl = [&](const char* url) {
		const std::string name = FilenameFromUrl(url);
		return SaveBinaryFromUrl(url, fs::path("Roms") / name, name.c_str(), sink);
	};

	if (userSelection == "Banjo-Kazooie") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Banjo-Kazooie%20(U)%20%5B!%5D.z64");
	}
	if (userSelection == "Diddy Kong Racing") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Diddy%20Kong%20Racing%20(USA)%20(En%2CFr)%20(Rev%201).z64");
	}
	if (userSelection == "Glover") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Glover%20(USA).z64");
	}
	if (userSelection == "GoldenEye007") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/GoldenEye%20007%20(USA).z64");
	}
	if (userSelection == "Mario Kart 64") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Mario%20Kart%2064%20(USA).z64");
	}
	if (userSelection == "Shotgun Mario 64") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Shotgun%20Mario%2064.z64");
	}
	if (userSelection == "Super Mario 64") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Super%20Mario%2064.n64");
	}
	if (userSelection == "Virtual Chess 64") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Virtual%20Chess%2064.n64");
	}
	if (userSelection == "Virtual Pool 64") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Virtual%20Pool%2064.n64");
	}
	if (userSelection == "Legend of Zelda, The - Ocarina of Time - Master Quest") {
		return dl(ROMFETCH_RAW_BASE
		          "Roms/N64/Legend%20of%20Zelda%2C%20The%20-%20Ocarina%20of%20Time%20-%20Master%20Quest%20(E)%20%5B!%5D.z64");
	}
	if (userSelection == "Paper Mario") {
		return dl(ROMFETCH_RAW_BASE "Roms/N64/Paper%20Mario%20(USA).z64");
	}
	if (userSelection == "Grand Theft Auto Advance") {
		return dl(ROMFETCH_RAW_BASE "Roms/GBA/Grand%20Theft%20Auto%20Advance%20(USA).gba");
	}
	if (userSelection == "Pokemon - Emerald Version") {
		return dl(ROMFETCH_RAW_BASE "Roms/GBA/Pokemon%20-%20Emerald%20Version%20(USA%2C%20Europe).gba");
	}
	if (userSelection == "Mickey Mania - The Timeless Adventures of Mickey Mouse") {
		return dl(ROMFETCH_RAW_BASE
		          "Roms/SNES/Mickey%20Mania%20-%20The%20Timeless%20Adventures%20of%20Mickey%20Mouse%20(USA).sfc");
	}
	if (userSelection == "Mortal Kombat") {
		return dl(ROMFETCH_RAW_BASE "Roms/SNES/Mortal%20Kombat%20(USA)%20(Rev%201).sfc");
	}
	if (userSelection == "Dr. Mario") {
		return dl(ROMFETCH_RAW_BASE "Roms/NES/Dr.%20Mario%20(Japan%2C%20USA)%20(Rev%201).nes");
	}
	if (userSelection == "Pokemon - Yellow Version - Special Pikachu Edition") {
		return dl(ROMFETCH_RAW_BASE
		          "Roms/GB/Pokemon%20-%20Yellow%20Version%20-%20Special%20Pikachu%20Edition%20(USA%2C%20Europe)%20(GBC%2CSGB%20Enhanced).gb");
	}
	if (userSelection == "Pokemon Red Version") {
		return dl(ROMFETCH_RAW_BASE "Roms/GBC/Pokemon%20Red%20GBC.gb");
	}
	return false;
}

} // namespace romfetch
