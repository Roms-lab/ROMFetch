#include <ImFrame.h>
#include <RomFetchCore.h>

#include <fstream>
#include <sstream>
#include <string>

#ifdef IMFRAME_WINDOWS
#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace
{
std::string StripCarriage(const std::string& line)
{
	std::string t = line;
	while (!t.empty() && (t.front() == '\r' || t.front() == '\n')) {
		t.erase(t.begin());
	}
	return t;
}
} // namespace

namespace GUI
{
	class MainApp : public ImFrame::ImApp
	{
	public:
		explicit MainApp(GLFWwindow* window) : ImFrame::ImApp(window)
		{
			// ImFrame enables docking + multi-viewports by default; that splits the UI into separate
			// draggable OS windows. ROMFetch should stay one GLFW window with a single uniform layout.
			{
				ImGuiIO& io = ImGui::GetIO();
				io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
				io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
			}
#if defined(_MSC_VER)
			romfetch::EnsureDllSearchPath();
#endif
			romfetch::Setup();
			romfetch::CheckForUpdates();
			LoadRomListText();
		}

		void LoadRomListText()
		{
			std::ifstream f("Assets/RomList.txt");
			if (!f) {
				m_romListText = "(Could not load Assets/RomList.txt)";
				return;
			}
			std::ostringstream ss;
			ss << f.rdbuf();
			m_romListText = ss.str();
		}

		void OnUpdate() override
		{
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(vp->WorkPos);
			ImGui::SetNextWindowSize(vp->WorkSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
			ImGui::Begin("ROMFetch", nullptr,
			             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			                 | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

			ImGui::TextWrapped("Type the ROM title exactly as in the list, then press Download.");
			ImGui::Spacing();
			ImGui::BeginChild("romlist", ImVec2(0.f, -140.f), false, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::TextUnformatted(m_romListText.c_str());
			ImGui::EndChild();

			ImGui::InputText("ROM name", m_inputBuf, sizeof(m_inputBuf));
			if (ImGui::Button("Download", ImVec2(-1.f, 0.f))) {
				m_progressLine.clear();
				m_resultMsg.clear();
				const std::string sel = m_inputBuf;
				const bool ok = romfetch::TryDownloadRom(sel, [this](const std::string& line) {
					const std::string t = StripCarriage(line);
					if (!t.empty()) {
						m_progressLine = t;
					}
				});
				if (ok) {
					m_resultMsg = "Finished download.";
				}
				else if (sel.empty()) {
					m_resultMsg = "Enter a ROM name.";
				}
				else {
					m_resultMsg = "Unknown ROM name or download failed.";
				}
			}

			if (!m_progressLine.empty()) {
				ImGui::TextWrapped("%s", m_progressLine.c_str());
			}
			if (!m_resultMsg.empty()) {
				ImGui::TextWrapped("%s", m_resultMsg.c_str());
			}

			ImGui::End();
			ImGui::PopStyleVar(2);
		}

		std::string m_romListText;
		char m_inputBuf[512]{};
		std::string m_progressLine;
		std::string m_resultMsg;
	};
} // namespace GUI

IMFRAME_MAIN("Roms-lab", "ROMFetch", GUI::MainApp)
