#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>

struct Node {
    enum { T_DIR, T_FILE, T_SYM } type;
    std::map<std::string, Node*> kids;
    std::string data;
    std::string link;
};

static Node *root = nullptr;
static std::string state_path;

static std::vector<std::string> split(const std::string &p) {
    std::vector<std::string> v;
    if (p.empty() || p[0] != '/') return v;
    size_t i = 1;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        if (j > i) v.push_back(p.substr(i, j - i));
        i = j + 1;
    }
    return v;
}

static Node* walk(const char *path) {
    if (!root) return nullptr;
    if (strcmp(path, "/") == 0) return root;
    auto parts = split(path);
    if (parts.empty()) return nullptr;
    Node *cur = root;
    for (auto &s : parts) {
        auto it = cur->kids.find(s);
        if (it == cur->kids.end()) return nullptr;
        cur = it->second;
    }
    return cur;
}

static Node* walk_parent(const char *path, std::string &name) {
    name.clear();
    auto parts = split(path);
    if (parts.empty()) return nullptr;
    Node *cur = root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        auto it = cur->kids.find(parts[i]);
        if (it == cur->kids.end()) return nullptr;
        if (it->second->type != Node::T_DIR) return nullptr;
        cur = it->second;
    }
    name = parts.back();
    return cur;
}

static bool name_ok(const std::string &s) {
    if (s.empty()) return false;
    if (s.size() > 255) return false;
    return s.find('/') == std::string::npos;
}

static void hexenc(const std::string &in, std::string &out) {
    static const char *h = "0123456789abcdef";
    out.clear();
    out.reserve(in.size()*2);
    for (unsigned char c: in) { out.push_back(h[c>>4]); out.push_back(h[c&15]); }
}
static bool hexdec(const std::string &in, std::string &out) {
    auto val=[&](char c)->int{
        if (c>='0'&&c<='9') return c-'0';
        if (c>='a'&&c<='f') return c-'a'+10;
        if (c>='A'&&c<='F') return c-'A'+10;
        return -1;
    };
    if (in.size()%2) return false;
    out.clear(); out.reserve(in.size()/2);
    for (size_t i=0;i<in.size();i+=2){
        int a=val(in[i]), b=val(in[i+1]); if(a<0||b<0) return false;
        out.push_back(char((a<<4)|b));
    }
    return true;
}

static void dump_rec(Node *n, const std::string &path, std::vector<std::string> &lines) {
    if (n->type == Node::T_DIR) {
        lines.push_back("D\t" + path);
        for (auto &kv : n->kids) dump_rec(kv.second, path + (path.size()>1?"/":"") + kv.first, lines);
    } else if (n->type == Node::T_FILE) {
        std::string h; hexenc(n->data, h);
        lines.push_back("F\t" + path + "\t" + h);
    } else {
        lines.push_back("S\t" + path + "\t" + n->link);
    }
}

static void save_state() {
    if (state_path.empty() || !root) return;
    std::vector<std::string> lines;
    dump_rec(root, std::string("/"), lines);
    std::ofstream f(state_path.c_str(), std::ios::trunc);
    for (auto &L : lines) f << L << "\n";
}

static Node* ensure_dir(const std::string &path) {
    if (path == "/") return root;
    auto parts = split(path);
    Node *cur = root;
    for (auto &s : parts) {
        auto it = cur->kids.find(s);
        if (it == cur->kids.end()) {
            Node *d = new Node(); d->type = Node::T_DIR;
            cur->kids[s] = d;
            cur = d;
        } else {
            cur = it->second;
            if (cur->type != Node::T_DIR) return nullptr;
        }
    }
    return cur;
}

static void load_state() {
    if (state_path.empty()) return;
    std::ifstream f(state_path.c_str());
    if (!f.good()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string t, p, extra;
        if (!std::getline(ss, t, '\t')) continue;
        if (!std::getline(ss, p, '\t')) continue;
        if (t=="D") {
            ensure_dir(p);
        } else if (t=="F") {
            std::getline(ss, extra, '\t');
            std::string data;
            if (!hexdec(extra, data)) data.clear();
            std::string name;
            Node *par = walk_parent(p.c_str(), name);
            if (!par || !name_ok(name)) continue;
            Node *n = new Node(); n->type = Node::T_FILE; n->data = data;
            par->kids[name] = n;
        } else if (t=="S") {
            std::getline(ss, extra, '\t');
            std::string name;
            Node *par = walk_parent(p.c_str(), name);
            if (!par || !name_ok(name)) continue;
            Node *n = new Node(); n->type = Node::T_SYM; n->link = extra;
            par->kids[name] = n;
        }
    }
}

static size_t count_files(Node *n) {
    size_t c = 0;
    if (n->type == Node::T_FILE) c += 1;
    if (n->type == Node::T_DIR) for (auto &kv : n->kids) c += count_files(kv.second);
    return c;
}

static size_t sum_bytes(Node *n) {
    if (n->type == Node::T_FILE) return n->data.size();
    size_t s = 0;
    if (n->type == Node::T_DIR) for (auto &kv : n->kids) s += sum_bytes(kv.second);
    return s;
}

static int memfs_getattr(const char *path, struct stat *st) {
    memset(st, 0, sizeof(*st));
    Node *n = walk(path);
    if (!n) return -ENOENT;
    if (n->type == Node::T_DIR) { st->st_mode = S_IFDIR | 0755; st->st_nlink = 2; st->st_size = 0; }
    else if (n->type == Node::T_FILE) { st->st_mode = S_IFREG | 0644; st->st_nlink = 1; st->st_size = n->data.size(); }
    else { st->st_mode = S_IFLNK | 0777; st->st_nlink = 1; st->st_size = n->link.size(); }
    return 0;
}

static int memfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *fi) {
    (void)off; (void)fi;
    Node *n = walk(path);
    if (!n) return -ENOENT;
    if (n->type != Node::T_DIR) return -ENOTDIR;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for (auto &kv : n->kids) filler(buf, kv.first.c_str(), NULL, 0);
    return 0;
}

static int memfs_mkdir(const char *path, mode_t mode) {
    (void)mode;
    std::string name;
    Node *par = walk_parent(path, name);
    if (!par) return -ENOENT;
    if (!name_ok(name)) return -ENAMETOOLONG;
    if (par->kids.count(name)) return -EEXIST;
    Node *d = new Node(); d->type = Node::T_DIR;
    par->kids[name] = d;
    save_state();
    return 0;
}

static int memfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    (void)rdev;
    if (!S_ISREG(mode)) return -EPERM;
    std::string name;
    Node *par = walk_parent(path, name);
    if (!par) return -ENOENT;
    if (!name_ok(name)) return -ENAMETOOLONG;
    if (par->kids.count(name)) return -EEXIST;
    Node *f = new Node(); f->type = Node::T_FILE;
    par->kids[name] = f;
    save_state();
    return 0;
}

static int memfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)fi;
    return memfs_mknod(path, S_IFREG | mode, 0);
}

static int memfs_open(const char *path, struct fuse_file_info *fi) {
    Node *n = walk(path);
    if (!n) return -ENOENT;
    if (n->type != Node::T_FILE) return -EISDIR;
    if (fi && (fi->flags & O_TRUNC)) { n->data.clear(); save_state(); }
    return 0;
}

static int memfs_read(const char *path, char *buf, size_t size, off_t off, struct fuse_file_info *fi) {
    (void)fi;
    Node *n = walk(path);
    if (!n) return -ENOENT;
    if (n->type != Node::T_FILE) return -EISDIR;
    if ((size_t)off >= n->data.size()) return 0;
    size_t can = n->data.size() - (size_t)off;
    if (size < can) can = size;
    memcpy(buf, n->data.data() + off, can);
    return (int)can;
}

static int memfs_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info *fi) {
    Node *n = walk(path);
    if (!n) return -ENOENT;
    if (n->type != Node::T_FILE) return -EISDIR;
    if (fi && (fi->flags & O_APPEND)) off = n->data.size();
    if ((size_t)off > n->data.size()) n->data.resize(off, '\0');
    if (n->data.size() < off + size) n->data.resize(off + size);
    memcpy(&n->data[off], buf, size);
    save_state();
    return (int)size;
}

static int memfs_symlink(const char *to, const char *from) {
    std::string name;
    Node *par = walk_parent(from, name);
    if (!par) return -ENOENT;
    if (!name_ok(name)) return -ENAMETOOLONG;
    if (par->kids.count(name)) return -EEXIST;
    Node *s = new Node(); s->type = Node::T_SYM; s->link = to;
    par->kids[name] = s;
    save_state();
    return 0;
}

static int memfs_readlink(const char *path, char *buf, size_t size) {
    Node *n = walk(path);
    if (!n) return -ENOENT;
    if (n->type != Node::T_SYM) return -EINVAL;
    size_t m = n->link.size();
    if (m > size) m = size;
    memcpy(buf, n->link.data(), m);
    return 0;
}

static int memfs_statfs(const char *path, struct statvfs *st) {
    (void)path;
    memset(st, 0, sizeof(*st));
    st->f_bsize = 1;
    st->f_frsize = 1;
    st->f_blocks = sum_bytes(root);
    st->f_files = count_files(root);
    st->f_namemax = 255;
    return 0;
}

static std::string parent_dir(const std::string &p) {
    if (p.empty()) return std::string("/tmp");
    size_t end = p.size();
    while (end > 1 && p[end-1] == '/') end--;
    size_t pos = p.rfind('/', end ? end-1 : 0);
    if (pos == std::string::npos) return std::string("/tmp");
    if (pos == 0) return std::string("/");
    return p.substr(0, pos);
}

static struct fuse_operations ops;

int main(int argc, char *argv[]) {
    root = new Node(); root->type = Node::T_DIR;
    std::string mp = (argc > 1) ? std::string(argv[argc-1]) : std::string("mnt");
    std::string base = parent_dir(mp);
    state_path = base + "/.memfs_state";
    load_state();

    ops.getattr = memfs_getattr;
    ops.readdir = memfs_readdir;
    ops.mkdir = memfs_mkdir;
    ops.mknod = memfs_mknod;
    ops.create = memfs_create;
    ops.open = memfs_open;
    ops.read = memfs_read;
    ops.write = memfs_write;
    ops.symlink = memfs_symlink;
    ops.readlink = memfs_readlink;
    ops.statfs = memfs_statfs;

    char *args[1024];
    int n = 0;
    args[n++] = argv[0];
    args[n++] = (char*)"-f";
    args[n++] = (char*)"-s";
    args[n++] = (char*)"-o";
    args[n++] = (char*)"fsname=memfs";
    for (int i = 1; i < argc; i++) args[n++] = argv[i];
    return fuse_main(n, args, &ops, NULL);
}
