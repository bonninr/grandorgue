#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <sys/resource.h>

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/image.h>

#include "config/GOConfig.h"
#include "GOOrganController.h"
#include "loader/GOProgressMonitor.h"
#include "GOOrgan.h"

class NullProgress : public GOProgressMonitor {
public:
  void Setup(long, const wxString &, const wxString &) override {}
  void Reset(long, const wxString &) override {}
  bool Update(unsigned, const wxString &) override { return true; }
};

static void print_mem(const char *tag, GOMemoryPool &pool) {
  std::cout << "=== " << tag << " ===\n";
  std::cout << "pool_alloc_MB=" << (pool.GetAllocSize() / (1024.0 * 1024.0)) << "\n";
  std::cout << "mapped_cache_MB=" << (pool.GetMappedSize() / (1024.0 * 1024.0)) << "\n";
  std::cout << "pool_usage_MB=" << (pool.GetPoolUsage() / (1024.0 * 1024.0)) << "\n";
  std::cout << "stream=" << (pool.IsStreamFromCache() ? "yes" : "no") << "\n";
  std::ifstream st("/proc/self/status");
  std::string line;
  while (std::getline(st, line)) {
    if (line.rfind("VmRSS:", 0) == 0 || line.rfind("VmSize:", 0) == 0 ||
        line.rfind("RssAnon:", 0) == 0 || line.rfind("RssFile:", 0) == 0)
      std::cout << line << "\n";
  }
  struct rusage ru {};
  getrusage(RUSAGE_SELF, &ru);
  std::cout << "ru_maxrss_KB=" << ru.ru_maxrss
            << " ru_minflt=" << ru.ru_minflt
            << " ru_majflt=" << ru.ru_majflt << "\n";
}

class GOLoadProbeApp : public wxApp {
public:
  bool OnInit() override {
    wxLog::SetActiveTarget(new wxLogStream(&std::cout));
    wxLog::SetLogLevel(wxLOG_Warning);
    wxImage::AddHandler(new wxJPEGHandler);
    wxImage::AddHandler(new wxPNGHandler);
    wxImage::AddHandler(new wxGIFHandler);
    wxImage::AddHandler(new wxBMPHandler);
    return true;
  }
  int OnRun() override {
    bool stream = false;
    unsigned head_kb = 256;
    wxString organPath;
    for (int i = 1; i < argc; i++) {
      wxString a = argv[i];
      if (a == "--stream")
        stream = true;
      else if (a == "--head-kb" && i + 1 < argc)
        head_kb = wxAtoi(argv[++i]);
      else if (!a.StartsWith("-"))
        organPath = a;
    }
    if (organPath.empty()) {
      std::cerr << "Usage: GOLoadProbe [--stream] [--head-kb N] <organ>\n";
      return 2;
    }
    std::cout << "organ=" << organPath << " stream=" << stream
              << " head_kb=" << head_kb << "\n";

    wxString confDir = wxT("/root/goconfig");
    wxString cacheDir = wxT("/root/gocache");
    wxFileName::Mkdir(confDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(cacheDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    std::string confPath = std::string(confDir.mb_str()) + "/GrandOrgue.conf";
    GOConfig settings("loadprobe", confPath);
    settings.Load();
    settings.ManageCache(true);
    settings.CompressCache(false);
    settings.StreamFromCache(stream);
    settings.StreamHeadKB(head_kb);
    settings.OrganCachePath(cacheDir);

    GOOrganController *ctrl = new GOOrganController(settings, true);
    GOOrgan organ(organPath);
    NullProgress mon;
    auto t0 = std::chrono::steady_clock::now();
    wxString err = ctrl->Load(organ, wxEmptyString, false, mon);
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    if (!err.empty()) {
      std::cerr << "LOAD ERROR: " << err << "\n";
      delete ctrl;
      return 1;
    }
    std::cout << "load_seconds=" << sec << "\n";
    std::cout << "cache_path=" << ctrl->GetCacheFilename() << "\n";
    print_mem("after_load", ctrl->GetMemoryPool());
    std::this_thread::sleep_for(std::chrono::seconds(3));
    print_mem("after_3s", ctrl->GetMemoryPool());
    delete ctrl;
    return 0;
  }
};
DECLARE_APP(GOLoadProbeApp)
IMPLEMENT_APP_CONSOLE(GOLoadProbeApp)