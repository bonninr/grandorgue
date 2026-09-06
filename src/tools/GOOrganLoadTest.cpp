/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

/*
 * Loads an organ and reports what came out, without starting audio or MIDI.
 *
 * The application asks the user to configure a sound device when it cannot
 * open one, which on a machine with no sound card means a dialog appears and
 * the organ is never reached - so the GUI is not the place to check whether a
 * definition loads. This builds the model directly, which is also what makes
 * the check fast enough to run on every change.
 */

#include <iostream>

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>

#include "config/GOConfig.h"
#include "loader/GOProgressMonitor.h"
#include "model/GOManual.h"
#include "model/GORank.h"
#include "model/GOSwitch.h"
#include "model/GOWindchest.h"
#include "model/GOWindchest.h"

#include "GOOrgan.h"
#include "GOOrganController.h"

class GOSilentProgress : public GOProgressMonitor {
public:
  void Setup(long, const wxString &, const wxString &) override {}
  void Reset(long, const wxString &) override {}
  bool Update(unsigned, const wxString &) override { return true; }
};

class GOOrganLoadTestApp : public wxApp {
public:
  bool OnInit() override {
    wxLog::SetActiveTarget(new wxLogStream(&std::cerr));
    wxImage::AddHandler(new wxJPEGHandler);
    wxImage::AddHandler(new wxPNGHandler);
    wxImage::AddHandler(new wxGIFHandler);
    wxImage::AddHandler(new wxBMPHandler);
    return true;
  }

  int OnRun() override {
    int result = 0;
    wxString organPath;
    wxString workDir;
    bool isWindModel = false;
    bool isNoVoicing = false;
    bool isNoSwitches = false;
    bool isNoTremulantModel = false;

    for (int i = 1; i < argc; i++) {
      const wxString arg = argv[i];

      if (arg == wxT("--work-dir") && i + 1 < argc)
        workDir = argv[++i];
      else if (arg == wxT("--wind-model"))
        isWindModel = true;
      else if (arg == wxT("--no-voicing"))
        isNoVoicing = true;
      else if (arg == wxT("--no-switches"))
        isNoSwitches = true;
      else if (arg == wxT("--no-tremulant-model"))
        isNoTremulantModel = true;
      else if (!arg.StartsWith(wxT("-")))
        organPath = arg;
    }

    if (organPath.IsEmpty()) {
      std::cerr << "Usage: GOOrganLoadTest [--work-dir DIR] <organ file>\n";
      result = 2;
    } else {
      if (workDir.IsEmpty())
        workDir = wxFileName::GetTempDir() + wxFileName::GetPathSeparator()
          + wxT("goloadtest");
      wxFileName::Mkdir(workDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

      const std::string confPath
        = std::string(workDir.mb_str()) + "/GrandOrgue.conf";
      GOConfig config("loadtest", confPath);

      config.Load();
      config.OrganCachePath(workDir);
      config.OrganSettingsPath(workDir);
      // Reading the samples would take minutes and is not what this checks;
      // the definition is either understood or it is not long before then.
      config.ManageCache(false);
      // Set here rather than in the config file so a run states its own
      // conditions and leaves nothing behind for the next one.
      config.HauptwerkWindModel(isWindModel);
      config.HauptwerkVoicing(!isNoVoicing);
      config.HauptwerkSwitches(!isNoSwitches);
      config.HauptwerkTremulantModel(!isNoTremulantModel);

      // True, not false: the panels are built during Load and reach for the
      // image cache, which only exists when the controller is told the
      // application is up. With false it is null and the load segfaults.
      GOOrganController controller(config, true);
      GOOrgan organ(organPath);
      GOSilentProgress monitor;
      const wxString errMsg = controller.Load(organ, wxEmptyString, true, monitor);

      if (!errMsg.IsEmpty()) {
        std::cout << "LOAD FAILED: " << errMsg.ToUTF8().data() << "\n";
        result = 1;
      } else {
        std::cout << "LOAD OK\n";
        std::cout << "  organ    : " << controller.GetOrganName().ToUTF8().data()
                  << "\n";
        std::cout << "  manuals  : " << controller.GetManualAndPedalCount()
                  << " (first " << controller.GetFirstManualIndex() << ")\n";
        std::cout << "  ranks    : " << controller.GetODFRankCount() << "\n";
        std::cout << "  windchsts: " << controller.GetWindchestCount() << "\n";
        std::cout << "  enclosurs: " << controller.GetEnclosureCount() << "\n";
        std::cout << "  tremulnts: " << controller.GetTremulantCount() << "\n";
        std::cout << "  switches : " << controller.GetSwitchCount() << "\n";

        unsigned nStops = 0;
        unsigned nCouplers = 0;

        for (unsigned manualI = controller.GetFirstManualIndex();
             manualI <= controller.GetManualAndPedalCount();
             manualI++) {
          GOManual *pManual = controller.GetManual(manualI);

          if (pManual) {
            nStops += pManual->GetStopCount();
            nCouplers += pManual->GetCouplerCount();
            std::cout << "  manual " << manualI << " : "
                      << pManual->GetName().ToUTF8().data() << ", "
                      << pManual->GetStopCount() << " stops, "
                      << pManual->GetCouplerCount() << " couplers, keys "
                      << pManual->GetFirstAccessibleKeyMIDINoteNumber() << "+"
                      << pManual->GetNumberOfAccessibleKeys() << "\n";
          }
        }
        std::cout << "  stops    : " << nStops << "\n";
        std::cout << "  couplers : " << nCouplers << "\n";
        std::cout << "  voicing  : " << (isNoVoicing ? "off" : "on")
                  << "\n";
        std::cout << "  windmodel: " << (isWindModel ? "on" : "off")
                  << "\n";

        unsigned nWindLimited = 0;

        for (unsigned n = controller.GetWindchestCount(), chestI = 0;
             chestI < n;
             chestI++) {
          GOWindchest *pChest = controller.GetWindchest(chestI);

          if (pChest && pChest->HasWindModel())
            nWindLimited++;
        }
        std::cout << "  wind-limited chests: " << nWindLimited << "\n";

        unsigned nDrawnSwitches = 0;
        unsigned nDerivedSwitches = 0;

        for (unsigned n = controller.GetSwitchCount(), switchI = 0;
             switchI < n;
             switchI++) {
          const GOSwitch *pSwitch = controller.GetSwitch(switchI);

          if (pSwitch) {
            if (pSwitch->IsDisplayed())
              nDrawnSwitches++;
            if (pSwitch->IsReadOnly())
              nDerivedSwitches++;
          }
        }
        std::cout << "  switches drawn: " << nDrawnSwitches
                  << ", derived: " << nDerivedSwitches << "\n";
      }
      controller.Clear();
    }
    return result;
  }
};

DECLARE_APP(GOOrganLoadTestApp)
IMPLEMENT_APP_CONSOLE(GOOrganLoadTestApp)
