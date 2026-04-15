#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <RomFetchCore.h>

int main() {
#ifdef _MSC_VER
	romfetch::EnsureDllSearchPath();
#endif
	romfetch::Setup();
	romfetch::CheckForUpdates();
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

	if (!romfetch::TryDownloadRom(UserSelection)) {
		printf("\n\nError, Invalid Rom Selection or download failed.\n\n");
	}
	printf("\nReloading Rom-Selection...");
	std::this_thread::sleep_for(std::chrono::seconds(1));
	goto RomSelection;
}
