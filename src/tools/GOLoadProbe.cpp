/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

/*
 * Loads an organ headless and reports what it cost, so the sample cache modes
 * can be compared without a sound card or a GUI.
 *
 * The number that matters is RssFile (the mapped cache actually resident)
 * versus RssAnon (data that had to be allocated). Streaming should move the
 * bulk from anon to file and let the kernel reclaim it; a bounded build should
 * keep ru_maxrss far below the size of the organ even on the very first run,
 * when the cache is still being written.
 */

#include <sys/resource.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <wx/app.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/stdpaths.h>

#include "config/GOConfig.h"
#include "loader/GOProgressMonitor.h"

#include "GOOrgan.h"
#include "GOOrganController.h"

class NullProgress : public GOProgressMonitor {
public:
  void Setup(long, const wxString &, const wxString &) override {}
  void Reset(long, const wxString &) override {}
  bool Update(unsigned, const wxString &) override { return true; }
};

static void print_mem(const char *tag, GOMemoryPool &pool) {
  std::cout << "=== " << tag << " ===\n";
  std::cout << "pool_alloc_MB=" << (pool.GetAllocSize() / (1024.0 * 1024.0))
            << "\n";
  std::cout << "mapped_cache_MB=" << (pool.GetMappedSize() / (1024.0 * 1024.0))
            << "\n";
  std::cout << "pool_usage_MB=" << (pool.GetPoolUsage() / (1024.0 * 1024.0))
            << "\n";
  std::cout << "stream=" << (pool.IsStreamFromCache() ? "yes" : "no") << "\n";

  std::ifstream st("/proc/self/status");
  std::string line;
  while (std::getline(st, line)) {
    if (
      line.rfind("VmRSS:", 0) == 0 || line.rfind("VmSize:", 0) == 0
      || line.rfind("RssAnon:", 0) == 0 || line.rfind("RssFile:", 0) == 0)
      std::cout << line << "\n";
  }

  struct rusage ru {};
  getrusage(RUSAGE_SELF, &ru);
  std::cout << "ru_maxrss_KB=" << ru.ru_maxrss << " ru_minflt=" << ru.ru_minflt
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
    bool boundedBuild = false;
    bool keepCache = false;
    unsigned head_kb = 256;
    wxString organPath;
    wxString workDir;

    for (int i = 1; i < argc; i++) {
      wxString a = argv[i];

      if (a == "--stream")
        stream = true;
      else if (a == "--bounded-build")
        boundedBuild = true;
      else if (a == "--keep-cache")
        keepCache = true;
      else if (a == "--head-kb" && i + 1 < argc)
        head_kb = wxAtoi(argv[++i]);
      else if (a == "--work-dir" && i + 1 < argc)
        workDir = argv[++i];
      else if (!a.StartsWith("-"))
        organPath = a;
    }

    if (organPath.empty()) {
      std::cerr << "Usage: GOLoadProbe [--stream] [--head-kb N] "
                   "[--bounded-build] [--keep-cache] [--work-dir DIR] "
                   "<organ file>\n";
      return 2;
    }

    if (workDir.empty())
      workDir = wxFileName::GetTempDir() + wxFileName::GetPathSeparator()
        + wxT("goloadprobe");

    wxString confDir = workDir + wxFileName::GetPathSeparator() + wxT("config");
    wxString cacheDir = workDir + wxFileName::GetPathSeparator() + wxT("cache");
    wxFileName::Mkdir(confDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(cacheDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    std::cout << "organ=" << organPath << "\nstream=" << stream
              << " head_kb=" << head_kb << " bounded_build=" << boundedBuild
              << "\nwork_dir=" << workDir << "\n";

    std::string confPath
      = std::string(confDir.mb_str()) + "/GrandOrgue.conf";
    GOConfig settings("loadprobe", confPath);
    settings.Load();
    settings.ManageCache(true);
    settings.CompressCache(false);
    settings.StreamFromCache(stream);
    settings.StreamHeadKB(head_kb);
    settings.BoundedCacheBuild(boundedBuild);
    settings.OrganCachePath(cacheDir);

    /* Unless asked otherwise start from a cold cache, so that a run measures
     * the build as well - which is the case a small machine actually fails on.
     * With --keep-cache a second run measures the load-from-cache path only. */
    if (!keepCache) {
      wxArrayString stale;
      wxDir::GetAllFiles(cacheDir, &stale);
      for (size_t i = 0; i < stale.GetCount(); i++)
        wxRemoveFile(stale[i]);
    }

    GOOrganController *ctrl = new GOOrganController(settings, true);
    GOOrgan organ(organPath);
    NullProgress mon;

    auto t0 = std::chrono::steady_clock::now();
    wxString err = ctrl->Load(organ, wxEmptyString, false, mon);
    auto t1 = std::chrono::steady_clock::now();

    if (!err.empty()) {
      std::cerr << "LOAD ERROR: " << err << "\n";
      delete ctrl;
      return 1;
    }

    std::cout << "load_seconds="
              << std::chrono::duration<double>(t1 - t0).count() << "\n";
    std::cout << "cache_path=" << ctrl->GetCacheFilename() << "\n";
    print_mem("after_load", ctrl->GetMemoryPool());

    /* Give the background touch thread a moment: without it the numbers above
     * can understate what the process settles at. */
    std::this_thread::sleep_for(std::chrono::seconds(3));
    print_mem("after_3s", ctrl->GetMemoryPool());

    delete ctrl;
    return 0;
  }
};

DECLARE_APP(GOLoadProbeApp)
IMPLEMENT_APP_CONSOLE(GOLoadProbeApp)
