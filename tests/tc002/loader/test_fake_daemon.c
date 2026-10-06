#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef AWTRIX_LOADER_TEST_ROOT
#error AWTRIX_LOADER_TEST_ROOT must name the harness directory
#endif

int main(int argc, char** argv) {
  FILE* out = fopen(AWTRIX_LOADER_TEST_ROOT "/daemon.out", "w");
  if (!out) return 1;
  const int own = fileno(out);
  for (int i = 0; i < argc; ++i) fprintf(out, "arg %s\n", argv[i]);
  DIR* directory = opendir("/proc/self/fd");
  const int listing = directory ? dirfd(directory) : -1;
  struct dirent* entry;
  while (directory && (entry = readdir(directory))) {
    if (entry->d_name[0] == '.') continue;
    const int fd = atoi(entry->d_name);
    if (fd != own && fd != listing) fprintf(out, "fd %d\n", fd);
  }
  if (directory) closedir(directory);
  struct sigaction pipe_action;
  sigaction(SIGPIPE, NULL, &pipe_action);
  sigset_t blocked;
  sigprocmask(SIG_SETMASK, NULL, &blocked);
  fprintf(out, "sigpipe %s\n", pipe_action.sa_handler == SIG_IGN ? "ignored" : "default");
  fprintf(out, "sigusr1 %s\n", sigismember(&blocked, SIGUSR1) ? "blocked" : "open");
  const char* workspace = getenv("ANDROID_PROPERTY_WORKSPACE");
  if (workspace) {
    char byte;
    fprintf(out, "workspace %s\n", pread(atoi(workspace), &byte, 1, 0) == 1 ? "readable" : "lost");
  }
  fclose(out);
  return 0;
}
