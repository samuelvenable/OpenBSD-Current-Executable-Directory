/*

 MIT License
 
 Copyright © 2025 Samuel Venable
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 
*/

// OpenBSD Current Executable Directory Implementation
// Compile: clang++ main.cpp -o a.out -std=c++17 -lkvm
// libkvm comes with OpenBSD; no additional dependency

#include <string>
#include <sstream>
#include <vector>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstddef>
#include <cstdlib>

#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <kvm.h>

std::string GetExecutableDirectory() {
  std::string path;
  auto is_exe = [](std::string exe) {
    int cntp = 0;
    std::string res;
    kvm_t *kd = nullptr;
    kinfo_file *kif = nullptr;
    bool error = false;
    kd = kvm_openfiles(nullptr, nullptr, nullptr, KVM_NO_FILES, nullptr);
    if (!kd) return res;
    if ((kif = kvm_getfiles(kd, KERN_FILE_BYPID, getpid(), sizeof(struct kinfo_file), &cntp))) {
      for (int i = 0; i < cntp && kif[i].fd_fd < 0; i++) {
        if (kif[i].fd_fd == KERN_FILE_TEXT) {
          struct stat st;
          fallback:
          char buffer[PATH_MAX];
          if (!stat(exe.c_str(), &st) && (st.st_mode & S_IXUSR) &&
            (st.st_mode & S_IFREG) && realpath(exe.c_str(), buffer) &&
            st.st_dev == (dev_t)kif[i].va_fsid && st.st_ino == (ino_t)kif[i].va_fileid) {
            res = buffer;
          }
          if (res.empty() && !error) {
            error = true;
            std::size_t last_slash_pos = exe.find_last_of("/");
            if (last_slash_pos != std::string::npos) {
              exe = exe.substr(0, last_slash_pos + 1) + kif[i].p_comm;
              goto fallback;
            }
          }
          break;
        }
      }
    }
    kvm_close(kd);
    return res;
  };
  auto cppstr_getenv = [](std::string name) {
    const char *cresult = getenv(name.c_str());
    std::string result = cresult ? cresult : "";
    return result;
  };
  int cntp = 0;
  kvm_t *kd = nullptr;
  kinfo_proc *proc_info = nullptr;
  std::vector<std::string> buffer;
  bool error = false, retried = false;
  kd = kvm_openfiles(nullptr, nullptr, nullptr, KVM_NO_FILES, nullptr);
  if (!kd) {
    path.clear();
    return path;
  }
  if ((proc_info = kvm_getprocs(kd, KERN_PROC_PID, getpid(), sizeof(struct kinfo_proc), &cntp))) {
    char **cmd = kvm_getargv(kd, proc_info, 0);
    if (cmd) {
      for (int i = 0; cmd[i]; i++) {
        buffer.push_back(cmd[i]);
      }
    }
  }
  kvm_close(kd);
  if (!buffer.empty()) {
    std::string argv0;
    if (!buffer[0].empty()) {
      fallback:
      std::size_t slash_pos = buffer[0].find('/');
      std::size_t colon_pos = buffer[0].find(':');
      if (slash_pos == 0) {
        argv0 = buffer[0];
        path = is_exe(argv0);
      } else if (slash_pos == std::string::npos || slash_pos > colon_pos) { 
        std::string penv = cppstr_getenv("PATH");
        if (!penv.empty()) {
          retry:
          std::string tmp;
          std::stringstream sstr(penv);
          while (std::getline(sstr, tmp, ':')) {
            argv0 = tmp + "/" + buffer[0];
            path = is_exe(argv0);
            if (!path.empty()) break;
            if (slash_pos > colon_pos) {
              argv0 = tmp + "/" + buffer[0].substr(0, colon_pos);
              path = is_exe(argv0);
              if (!path.empty()) break;
            }
          }
        }
        if (path.empty() && !retried) {
          retried = true;
          penv = "/usr/bin:/bin:/usr/sbin:/sbin:/usr/X11R6/bin:/usr/local/bin:/usr/local/sbin";
          std::string home = cppstr_getenv("HOME");
          if (!home.empty()) {
            penv = home + "/bin:" + penv;
          }
          goto retry;
        }
      }
      if (path.empty() && slash_pos > 0) {
        std::string pwd = cppstr_getenv("PWD");
        if (!pwd.empty()) {
          argv0 = pwd + "/" + buffer[0];
          path = is_exe(argv0);
        }
        if (path.empty()) {
          char cwd[PATH_MAX];
          if (getcwd(cwd, PATH_MAX)) {
            argv0 = std::string(cwd) + "/" + buffer[0];
            path = is_exe(argv0);
          }
        }
      }
    }
    if (path.empty() && !error) {
      error = true;
      buffer.clear();
      std::string underscore = cppstr_getenv("_");
      if (!underscore.empty()) {
        buffer.push_back(underscore);
        goto fallback;
      }
    }
  }
  if (!path.empty()) {
    errno = 0;
  }
  std::size_t pos = path.find_last_of("/");
  if (pos != std::string::npos) {
    path = path.substr(0, pos + 1);
  }
  return path;
}

int main() {
  std::string exe = GetExecutableDirectory();
  bool failed = exe.empty();
  if (!failed) {
    printf("GetExecutableDirectory() Result: %s\n", exe.c_str());
  } else {
    printf("GetExecutableDirectory() Error: %s\n", strerror(errno));
  }
  return failed;
}
