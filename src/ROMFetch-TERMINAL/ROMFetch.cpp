#include <cstdlib>
#include <cstdio>
#include <string>
#include <string_view>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>
#include <Windows.h>
#ifdef _MSC_VER
#include <string>
#endif
#include <cpr/cpr.h>

#ifdef _MSC_VER
// Delay-loaded cpr.dll / libcurl.dll: search DLL/ beside the executable (see CMake /DELAYLOAD).
static void EnsureDllSearchPath() {
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
#endif

namespace fs = std::filesystem;

namespace {

// https://github.com/Roms-lab/ROMFetch — metadata/, Update/, Roms/<platform>/
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

/** Single-line progress (\\r); call printf("\\n") after the request finishes. */
cpr::ProgressCallback MakeProgressCallback(std::string label) {
	auto lastPrint = std::make_shared<std::chrono::steady_clock::time_point>();
	auto havePrinted = std::make_shared<bool>(false);
	return cpr::ProgressCallback{[label = std::move(label), lastPrint, havePrinted](
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
			printf("\r  [%s]  %lld / %lld bytes (%.1f%%)   ", label.c_str(), static_cast<long long>(dlnow),
			       static_cast<long long>(dltotal), pct);
		}
		else if (dlnow > 0) {
			printf("\r  [%s]  %lld bytes (total size unknown)...   ", label.c_str(), static_cast<long long>(dlnow));
		}
		else {
			printf("\r  [%s]  connecting...   ", label.c_str());
		}
		fflush(stdout);
		return true;
	}};
}

bool SaveTextFromUrl(const char* url, const fs::path& path, const char* progress_label = nullptr) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::string label = progress_label ? std::string(progress_label) : path.filename().string();
	cpr::Response r = cpr::Get(cpr::Url{url}, MakeProgressCallback(std::move(label)));
	printf("\n");
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

bool SaveBinaryFromUrl(const char* url, const fs::path& path, const char* progress_label = nullptr) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	const std::string label = progress_label ? std::string(progress_label) : path.filename().string();
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	cpr::Response r = cpr::Download(out, cpr::Url{url}, MakeProgressCallback(std::move(label)));
	printf("\n");
	return HttpOk(r);
}

bool DownloadRom(const char* url) {
	const std::string name = FilenameFromUrl(url);
	return SaveBinaryFromUrl(url, fs::path("Roms") / name, name.c_str());
}

void RemoveTreeIfExists(const fs::path& p) {
	std::error_code ec;
	fs::remove_all(p, ec);
}

} // namespace

int UpdateBox1;

void Setup() {
	RemoveTreeIfExists("Assets");
	std::error_code ec;
	fs::create_directories("Roms", ec);
	fs::create_directories("Assets", ec);
	SaveTextFromUrl(kRomListUrl, "Assets/RomList.txt", "Rom list");
}

void CheckForUpdates() {
	std::string Version = "0.0.3";
	std::string LatestVersion;
	SaveTextFromUrl(kVersionUrl, "Assets/Version.txt", "Version");
	std::ifstream VersionFile("Assets/Version.txt");
	if (!VersionFile.is_open()) {
		printf("Error, cannot check updates.");
		return;
	}
	std::getline(VersionFile, LatestVersion);
	VersionFile.close();
	if (Version != LatestVersion) {
		UpdateBox1 = MessageBoxA(NULL, "New version is available, you should update to the latest version for the best experience.", "ROMFetch", MB_OK | MB_ICONINFORMATION);
	}
}

int main() {
#ifdef _MSC_VER
	EnsureDllSearchPath();
#endif
	Setup();
	CheckForUpdates();
	RomSelection:
	system("cls");
	printf("[--- ROMFetch Available Roms ---]\n\n");
	if (std::ifstream list("Assets/RomList.txt"); list) {
		std::cout << list.rdbuf();
	}
	printf("\n");
	printf("\nType Rom Name EXACTLY as it appears to download.\n\n");
	printf("> ");
	std::string UserSelection;
	std::getline(std::cin, UserSelection);

	// N64 Roms — Roms/N64/
	if (UserSelection == "Banjo-Kazooie") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Banjo-Kazooie%20(U)%20%5B!%5D.z64");
	}
	else if (UserSelection == "Diddy Kong Racing") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Diddy%20Kong%20Racing%20(USA)%20(En%2CFr)%20(Rev%201).z64");
	}
	else if (UserSelection == "Glover") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Glover%20(USA).z64");
	}
	else if (UserSelection == "GoldenEye007") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/GoldenEye%20007%20(USA).z64");
	}
	else if (UserSelection == "Mario Kart 64") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Mario%20Kart%2064%20(USA).z64");
	}
	else if (UserSelection == "Shotgun Mario 64") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Shotgun%20Mario%2064.z64");
	}
	else if (UserSelection == "Super Mario 64") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Super%20Mario%2064.n64");
	}
	else if (UserSelection == "Virtual Chess 64") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Virtual%20Chess%2064.n64");
	}
	else if (UserSelection == "Virtual Pool 64") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Virtual%20Pool%2064.n64");
	}
	else if (UserSelection == "Legend of Zelda, The - Ocarina of Time - Master Quest") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Legend%20of%20Zelda%2C%20The%20-%20Ocarina%20of%20Time%20-%20Master%20Quest%20(E)%20%5B!%5D.z64");
	}
	else if (UserSelection == "Paper Mario") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/N64/Paper%20Mario%20(USA).z64");
	}
	// GBA Roms — Roms/GBA/
	else if (UserSelection == "Grand Theft Auto Advance") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/GBA/Grand%20Theft%20Auto%20Advance%20(USA).gba");
	}
	else if (UserSelection == "Pokemon - Emerald Version") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/GBA/Pokemon%20-%20Emerald%20Version%20(USA%2C%20Europe).gba");
	}
	// SNES Roms — Roms/SNES/
	else if (UserSelection == "Mickey Mania - The Timeless Adventures of Mickey Mouse") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/SNES/Mickey%20Mania%20-%20The%20Timeless%20Adventures%20of%20Mickey%20Mouse%20(USA).sfc");
	}
	else if (UserSelection == "Mortal Kombat") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/SNES/Mortal%20Kombat%20(USA)%20(Rev%201).sfc");
	}
	// NES Roms — Roms/NES/
	else if (UserSelection == "Dr. Mario") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/NES/Dr.%20Mario%20(Japan%2C%20USA)%20(Rev%201).nes");
	}
	// GB Roms — Roms/GB/
	else if (UserSelection == "Pokemon - Yellow Version - Special Pikachu Edition") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/GB/Pokemon%20-%20Yellow%20Version%20-%20Special%20Pikachu%20Edition%20(USA%2C%20Europe)%20(GBC%2CSGB%20Enhanced).gb");
	}
	// GBC Roms — Roms/GBC/
	else if (UserSelection == "Pokemon Red Version") {
		DownloadRom(ROMFETCH_RAW_BASE "Roms/GBC/Pokemon%20Red%20GBC.gb");
	}
	else {
		printf("\n\nError, Invalid Rom Selection.\n\n");
	}
	printf("\nReloading Rom-Selection...");
	std::this_thread::sleep_for(std::chrono::seconds(1));
	goto RomSelection;
}
