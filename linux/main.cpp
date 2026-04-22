// Stacks IDE — Linux desktop shell
//
// Uses WebKitGTK 6 (GTK4) to host the web UI. Also spawns stacksd on launch.
//
// Build: see linux/CMakeLists.txt. Requires:
//   * gtk4, libwebkitgtk-6.0-dev
//
//   sudo apt install libgtk-4-dev libwebkitgtk-6.0-dev cmake g++

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

namespace fs = std::filesystem;

static pid_t g_daemon_pid = 0;

static std::string exe_dir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) return ".";
    buf[n] = 0;
    return fs::path(buf).parent_path().string();
}

static void start_daemon() {
    auto exe = exe_dir() + "/stacksd";
    if (!fs::exists(exe)) return;
    pid_t pid = fork();
    if (pid == 0) {
        execl(exe.c_str(), exe.c_str(), (char*)nullptr);
        _exit(127);
    }
    if (pid > 0) g_daemon_pid = pid;
}

static void stop_daemon() {
    if (g_daemon_pid > 0) { kill(g_daemon_pid, SIGTERM); g_daemon_pid = 0; }
}

static void on_activate(GtkApplication* app, gpointer) {
    GtkWidget* win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "Stacks IDE");
    gtk_window_set_default_size(GTK_WINDOW(win), 1280, 820);

    WebKitWebView* view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    WebKitSettings* s = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_developer_extras(s, TRUE);
    webkit_settings_set_javascript_can_access_clipboard(s, TRUE);

    std::string url = "file://" + exe_dir() + "/web/index.html";
    webkit_web_view_load_uri(view, url.c_str());

    gtk_window_set_child(GTK_WINDOW(win), GTK_WIDGET(view));
    gtk_window_present(GTK_WINDOW(win));

    g_signal_connect(win, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        stop_daemon();
    }), nullptr);
}

int main(int argc, char** argv) {
    start_daemon();
    GtkApplication* app = gtk_application_new("dev.stacks.ide", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    stop_daemon();
    return status;
}
