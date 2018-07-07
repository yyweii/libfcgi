#include <signal.h>
#include <unistd.h>
#include "fcgi_app.h"
#include "fcgi_request.h"

bool s_stop_process = false;

void my_sa_handler(int /*signum*/) { s_stop_process = true; }

void set_sig_handler() {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGQUIT);
  sigaddset(&sa.sa_mask, SIGINT);
  sa.sa_sigaction = nullptr;
  sa.sa_handler = my_sa_handler;
  sa.sa_flags = 0;
  sa.sa_restorer = nullptr;

  sigaction(SIGQUIT, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
}

int main(int, char **) {
  set_sig_handler();

  FcgiApp::new_instance();
  FcgiApp::instance()->start(2);

  while (!s_stop_process) {
    auto req = FcgiApp::instance()->pop_request_nonblocking();
    if (req == nullptr) {
      sleep(1);
      continue;
    }

    std::string str("Content-type: text/html; charset=utf-8\r\n\r\n");
    str += req->stdin() + "\n";
    req->stdout(str);
    req->end_stdout();
    req->reply(0);

    FcgiApp::instance()->free_request(req);
  }

  FcgiApp::delete_instance();
  return 0;
}
