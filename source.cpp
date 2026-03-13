#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <fcntl.h>

using namespace std;

// ======================= OS Detection =======================
enum OS_TYPE { UNKNOWN, MACOS, DEBIAN, FEDORA };

mutex cout_mutex;

string to_lower_copy(string s) {
    transform(s.begin(), s.end(), s.begin(),
              [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
}

OS_TYPE detect_os() {
#ifdef __APPLE__
    return MACOS;
#elif __linux__
    ifstream os_file("/etc/os-release");
    string line;
    string id;
    string id_like;

    while (getline(os_file, line)) {
        if (line.rfind("ID=", 0) == 0) {
            id = to_lower_copy(line.substr(3));
            if (!id.empty() && id.front() == '"' && id.back() == '"') {
                id = id.substr(1, id.size() - 2);
            }
        } else if (line.rfind("ID_LIKE=", 0) == 0) {
            id_like = to_lower_copy(line.substr(8));
            if (!id_like.empty() && id_like.front() == '"' && id_like.back() == '"') {
                id_like = id_like.substr(1, id_like.size() - 2);
            }
        }
    }

    if (id.find("fedora") != string::npos || id_like.find("fedora") != string::npos ||
        id_like.find("rhel") != string::npos || id_like.find("centos") != string::npos) {
        return FEDORA;
    }

    if (id.find("debian") != string::npos || id.find("ubuntu") != string::npos ||
        id_like.find("debian") != string::npos || id_like.find("ubuntu") != string::npos) {
        return DEBIAN;
    }

    return UNKNOWN;
#else
    return UNKNOWN;
#endif
}

string os_name(OS_TYPE os) {
    switch (os) {
        case MACOS: return "macOS";
        case DEBIAN: return "Debian/Ubuntu";
        case FEDORA: return "Fedora/RHEL-like";
        default: return "Unknown";
    }
}

// ======================= Helpers ===========================
vector<string> split_path(const string &path) {
    vector<string> dirs;
    string current;
    for (char c : path) {
        if (c == ':') {
            dirs.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    dirs.push_back(current);
    return dirs;
}

bool executable_exists_at(const string &path) {
    return access(path.c_str(), X_OK) == 0;
}

string find_executable(const string &cmd) {
    if (cmd.find('/') != string::npos) {
        return executable_exists_at(cmd) ? cmd : "";
    }

    const char *path_env = getenv("PATH");
    if (!path_env) return "";

    for (const string &dir : split_path(path_env)) {
        if (dir.empty()) continue;
        string candidate = dir + "/" + cmd;
        if (executable_exists_at(candidate)) return candidate;
    }

    return "";
}

bool command_exists(const string &cmd) {
    return !find_executable(cmd).empty();
}

string locate_brew() {
    string from_path = find_executable("brew");
    if (!from_path.empty()) return from_path;

    vector<string> common_paths = {
        "/opt/homebrew/bin/brew",
        "/usr/local/bin/brew",
        "/home/linuxbrew/.linuxbrew/bin/brew"
    };

    for (const auto &path : common_paths) {
        if (executable_exists_at(path)) return path;
    }

    return "";
}

bool is_safe_package_token(const string &s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!(isalnum(c) || c == '.' || c == '_' || c == '+' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool is_safe_host_arg(const string &s) {
    if (s.empty()) return false;
    if (s[0] == '-') return false;

    for (unsigned char c : s) {
        if (!(isalnum(c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']')) {
            return false;
        }
    }
    return true;
}

int run_exec(const vector<string> &args) {
    if (args.empty()) return 1;

    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "[*] Running:";
        for (const auto &arg : args) cout << ' ' << arg;
        cout << endl;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        perror("execvp");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

bool can_elevate() {
    return geteuid() == 0 || command_exists("sudo");
}

vector<string> prefixed_with_privilege(const vector<string> &args) {
    if (geteuid() == 0) return args;
    vector<string> out = {"sudo"};
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

// ======================= Package Mapping ====================
vector<string> resolve_package_alias(const string &pkg, OS_TYPE os) {
    string p = to_lower_copy(pkg);

    if (p == "brew") return {"brew"};

    if (p == "node" || p == "nodejs") {
        if (os == MACOS) return {"node"};
        return {"nodejs"};
    }

    if (p == "npm") {
        if (os == MACOS) return {"node"};
        return {"npm"};
    }

    if (p == "python" || p == "python3") {
        if (os == MACOS) return {"python"};
        return {"python3"};
    }

    if (p == "pip" || p == "pip3") {
        if (os == MACOS) return {"python"};
        return {"python3-pip"};
    }

    if (p == "dig" || p == "dnsutils" || p == "bind-utils" || p == "bind9-dnsutils") {
        if (os == MACOS) return {"bind"};
        if (os == DEBIAN) return {"bind9-dnsutils"};
        if (os == FEDORA) return {"bind-utils"};
    }

    if (p == "pkg-config" || p == "pkgconf") {
        if (os == FEDORA) return {"pkgconf-pkg-config"};
        return {"pkg-config"};
    }

    if (p == "build-essential" || p == "devtools" || p == "toolchain" || p == "cpp" || p == "c++") {
        if (os == DEBIAN) return {"build-essential"};
        if (os == FEDORA) return {"gcc", "gcc-c++", "make"};
        return {};
    }

    return {pkg};
}

vector<string> resolve_packages(const vector<string> &requested, OS_TYPE os) {
    set<string> seen;
    vector<string> resolved;

    for (const auto &req : requested) {
        vector<string> mapped = resolve_package_alias(req, os);
        for (const auto &pkg : mapped) {
            if (!pkg.empty() && !seen.count(pkg)) {
                seen.insert(pkg);
                resolved.push_back(pkg);
            }
        }
    }

    return resolved;
}

int install_brew_if_needed() {
    string brew_path = locate_brew();
    if (!brew_path.empty()) return 0;

    return run_exec({
        "/bin/bash",
        "-c",
        "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    });
}

int brew_install_packages(const vector<string> &pkgs) {
    if (pkgs.empty()) return 0;

    if (install_brew_if_needed() != 0) return 1;

    string brew_path = locate_brew();
    if (brew_path.empty()) {
        cerr << "[!] Homebrew installation completed, but brew was not found in PATH or common locations." << endl;
        return 1;
    }

    vector<string> args = {brew_path, "install"};
    args.insert(args.end(), pkgs.begin(), pkgs.end());
    return run_exec(args);
}

int apt_install_packages(const vector<string> &pkgs) {
    if (pkgs.empty()) return 0;
    if (!can_elevate()) {
        cerr << "[!] Need root or sudo to install packages." << endl;
        return 1;
    }

    int rc = run_exec(prefixed_with_privilege({"apt", "update"}));
    if (rc != 0) return rc;

    vector<string> install_cmd = {"apt", "install", "-y"};
    install_cmd.insert(install_cmd.end(), pkgs.begin(), pkgs.end());
    return run_exec(prefixed_with_privilege(install_cmd));
}

int dnf_install_packages(const vector<string> &pkgs) {
    if (pkgs.empty()) return 0;
    if (!can_elevate()) {
        cerr << "[!] Need root or sudo to install packages." << endl;
        return 1;
    }

    vector<string> install_cmd = {"dnf", "install", "-y"};
    install_cmd.insert(install_cmd.end(), pkgs.begin(), pkgs.end());
    return run_exec(prefixed_with_privilege(install_cmd));
}

int install_packages(const vector<string> &requested, OS_TYPE os) {
    if (os == UNKNOWN) {
        cerr << "[!] Unsupported or unknown OS. Refusing to guess a package manager." << endl;
        return 1;
    }

    vector<string> pkgs = resolve_packages(requested, os);
    if (pkgs.empty()) {
        cout << "[*] Nothing to install." << endl;
        return 0;
    }

    if (os == MACOS) {
        vector<string> brew_pkgs;
        for (const auto &pkg : pkgs) {
            if (pkg != "brew") brew_pkgs.push_back(pkg);
        }
        if (pkgs.size() == 1 && pkgs[0] == "brew") return install_brew_if_needed();
        return brew_install_packages(brew_pkgs);
    }

    if (os == DEBIAN) return apt_install_packages(pkgs);
    if (os == FEDORA) return dnf_install_packages(pkgs);
    return 1;
}

void install_package(const string &pkg, OS_TYPE os) {
    if (!is_safe_package_token(pkg)) {
        cerr << "[!] Unsafe package name rejected." << endl;
        return;
    }
    (void)install_packages({pkg}, os);
}

void update_all(OS_TYPE os) {
    if (os == MACOS) {
        if (install_brew_if_needed() != 0) return;
        string brew_path = locate_brew();
        if (brew_path.empty()) {
            cerr << "[!] brew not found." << endl;
            return;
        }
        run_exec({brew_path, "update"});
        run_exec({brew_path, "upgrade"});
        return;
    }

    if (os == DEBIAN) {
        if (!can_elevate()) {
            cerr << "[!] Need root or sudo to update packages." << endl;
            return;
        }
        run_exec(prefixed_with_privilege({"apt", "update"}));
        run_exec(prefixed_with_privilege({"apt", "upgrade", "-y"}));
        return;
    }

    if (os == FEDORA) {
        if (!can_elevate()) {
            cerr << "[!] Need root or sudo to update packages." << endl;
            return;
        }
        run_exec(prefixed_with_privilege({"dnf", "upgrade", "-y"}));
        return;
    }

    cerr << "[!] Unsupported or unknown OS." << endl;
}

void setup_all(OS_TYPE os) {
    cout << "[*] Installing common developer and utility packages..." << endl;

    vector<string> core = {
        "git",
        "curl",
        "wget",
        "nodejs",
        "npm",
        "python3",
        "pip",
        "jq",
        "zip",
        "unzip",
        "tmux",
        "tree",
        "htop",
        "ripgrep",
        "cmake",
        "pkg-config",
        "build-essential",
        "dig",
        "whois",
        "traceroute",
        "nmap"
    };

    vector<string> optional = {
        "fastfetch",
        "neofetch",
        "cmatrix"
    };

    int rc = install_packages(core, os);
    if (rc != 0) {
        cerr << "[!] Core package installation failed. Optional extras were not attempted." << endl;
        return;
    }

    cout << "[*] Installing optional extras..." << endl;
    for (const auto &pkg : optional) {
        int extra_rc = install_packages({pkg}, os);
        if (extra_rc != 0) {
            lock_guard<mutex> lock(cout_mutex);
            cout << "[!] Optional package skipped or unavailable: " << pkg << endl;
        }
    }

    cout << "[*] Setup complete!" << endl;
}

// ======================= Network Scan =======================
bool connect_with_timeout(int sock, const sockaddr_in &target, int timeout_ms) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) return false;

    int result = connect(sock, reinterpret_cast<const sockaddr *>(&target), sizeof(target));
    if (result == 0) {
        fcntl(sock, F_SETFL, flags);
        return true;
    }

    if (errno != EINPROGRESS) return false;

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    timeval tv {};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    result = select(sock + 1, nullptr, &writefds, nullptr, &tv);
    if (result <= 0) return false;

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) return false;
    if (so_error != 0) return false;

    fcntl(sock, F_SETFL, flags);
    return true;
}

string grab_banner(int sock, int timeout_ms = 700) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    timeval tv {};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = select(sock + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0) return "No banner";

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) return "No banner";

    string banner(buffer, len);
    size_t pos = banner.find_first_of("\r\n");
    if (pos != string::npos) banner = banner.substr(0, pos);
    return banner;
}

bool is_valid_ipv4(const string &ip) {
    sockaddr_in sa {};
    return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) == 1;
}

void check_port(const string &ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[!] Failed to create socket" << endl;
        return;
    }

    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &target.sin_addr);

    bool open = connect_with_timeout(sock, target, 2000);
    string banner = open ? grab_banner(sock) : "";

    {
        lock_guard<mutex> lock(cout_mutex);
        if (open) {
            cout << port << "/tcp \033[32mOPEN\033[0m";
            if (banner != "No banner" && !banner.empty()) cout << " | " << banner;
            cout << endl;
        } else {
            cout << port << "/tcp \033[31mCLOSED\033[0m" << endl;
        }
    }

    close(sock);
}

void scan_host(const string &ip) {
    vector<int> ports = {21, 22, 23, 25, 53, 80, 110, 143, 443, 3306, 8080};
    cout << "[*] Scanning " << ip << "...\n";

    vector<thread> threads;
    for (int port : ports) {
        threads.emplace_back([=] { check_port(ip, port); });
    }
    for (auto &t : threads) t.join();
}

// ======================= DNS / Whois / Trace =======================
void dns_lookup(const string &host) {
    if (!is_safe_host_arg(host)) {
        cerr << "[!] Invalid host.\n";
        return;
    }
    cout << "[*] DNS lookup for " << host << ":\n";
    run_exec({"dig", host, "+short"});
}

void whois_lookup(const string &host) {
    if (!is_safe_host_arg(host)) {
        cerr << "[!] Invalid host.\n";
        return;
    }
    cout << "[*] WHOIS lookup for " << host << ":\n";
    run_exec({"whois", host});
}

void traceroute_host(const string &host) {
    if (!is_safe_host_arg(host)) {
        cerr << "[!] Invalid host.\n";
        return;
    }
    cout << "[*] Traceroute to " << host << ":\n";
    run_exec({"traceroute", host});
}

// ======================= Help ==============================
void show_help() {
    cout << "\nXSH - Utility CLI\n\n";
    cout << "Detected OS: " << os_name(detect_os()) << "\n\n";
    cout << "Commands:\n";
    cout << "  help                     Show this help menu\n";
    cout << "  install <package>        Install a package or alias\n";
    cout << "  update all               Update all packages\n";
    cout << "  setup all                Install common dev + utility packages\n";
    cout << "  scan <ip>                Scan common ports with banners\n";
    cout << "  port <ip> <port>         Test a specific port with banner\n";
    cout << "  dns <host>               DNS lookup\n";
    cout << "  whois <host>             WHOIS lookup\n";
    cout << "  trace <host>             Traceroute\n";
    cout << "\nAliases:\n";
    cout << "  h -> help\n";
    cout << "  i -> install\n";
    cout << "\nUseful install aliases:\n";
    cout << "  node / nodejs / npm\n";
    cout << "  python / python3 / pip\n";
    cout << "  dig / dnsutils\n";
    cout << "  build-essential / devtools\n";
    cout << "\n";
}

// ======================= Main ==============================
int main(int argc, char *argv[]) {
    OS_TYPE os = detect_os();

    if (argc < 2) {
        if (os == UNKNOWN) {
            cerr << "[!] Warning: unsupported or unrecognized OS. Package management commands are disabled.\n";
        }
        show_help();
        return 0;
    }

    string command = argv[1];

    if (command == "help" || command == "h") {
        show_help();
    } else if (command == "install" || command == "i") {
        if (argc < 3) {
            cout << "Usage: xsh install <package>\n";
            return 1;
        }
        install_package(argv[2], os);
    } else if (command == "update") {
        if (argc >= 3 && string(argv[2]) == "all") update_all(os);
        else cout << "Usage: xsh update all\n";
    } else if (command == "setup") {
        if (argc >= 3 && string(argv[2]) == "all") setup_all(os);
        else cout << "Usage: xsh setup all\n";
    } else if (command == "scan") {
        if (argc < 3) {
            cout << "Usage: xsh scan <ip>\n";
            return 1;
        }
        string ip = argv[2];
        if (!is_valid_ipv4(ip)) {
            cerr << "[!] scan currently supports IPv4 addresses only.\n";
            return 1;
        }
        scan_host(ip);
    } else if (command == "port") {
        if (argc < 4) {
            cout << "Usage: xsh port <ip> <port>\n";
            return 1;
        }

        string ip = argv[2];
        if (!is_valid_ipv4(ip)) {
            cerr << "[!] port currently supports IPv4 addresses only.\n";
            return 1;
        }

        int port = 0;
        try {
            size_t consumed = 0;
            port = stoi(argv[3], &consumed);
            if (consumed != string(argv[3]).size()) throw invalid_argument("extra chars");
        } catch (...) {
            cerr << "[!] Invalid port. Must be a number from 1 to 65535.\n";
            return 1;
        }

        if (port < 1 || port > 65535) {
            cerr << "[!] Invalid port. Must be a number from 1 to 65535.\n";
            return 1;
        }

        check_port(ip, port);
    } else if (command == "dns") {
        if (argc < 3) {
            cout << "Usage: xsh dns <host>\n";
            return 1;
        }
        dns_lookup(argv[2]);
    } else if (command == "whois") {
        if (argc < 3) {
            cout << "Usage: xsh whois <host>\n";
            return 1;
        }
        whois_lookup(argv[2]);
    } else if (command == "trace") {
        if (argc < 3) {
            cout << "Usage: xsh trace <host>\n";
            return 1;
        }
        traceroute_host(argv[2]);
    } else {
        cout << "Unknown command\n";
        show_help();
    }

    return 0;
}
