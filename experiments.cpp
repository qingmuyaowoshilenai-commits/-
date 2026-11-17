// 缓存场景下数组与链表性能对比实验
// 实验1: 顺序访问（缓存友好）  实验2: 随机访问（缓存不友好）  实验3: LRU 缓存实现对比
// 编译: g++ -O2 -std=c++17 -o experiments experiments.cpp
#include <bits/stdc++.h>
using namespace std;
using clk = chrono::steady_clock;

// ---------- 通用工具 ----------
static long long sink = 0;  // 防止编译器优化掉计算

inline void emit_row(const string& path, vector<string> row, bool header = false) {
    ofstream f(path, header ? ios::trunc : ios::app);
    if (header) {
        for (size_t i = 0; i < row.size(); ++i)
            f << (i ? "," : "") << row[i];
        f << "\n";
    }
    for (size_t i = 0; i < row.size(); ++i)
        f << (i ? "," : "") << row[i];
    f << "\n";
}

double time_us(function<void()> fn) {
    auto t0 = clk::now();
    fn();
    auto t1 = clk::now();
    return chrono::duration<double, micro>(t1 - t0).count();
}

// ---------- 链表节点：32 字节，模拟缓存行利用率低 ----------
struct Node {
    Node* next;
    long long pad[3];  // 填充至 32B
};

// 构建随机链接顺序的链表：节点地址随机分布，链接顺序随机打乱
Node* build_list(size_t n, bool shuffle_links) {
    vector<Node*> ptrs(n);
    for (size_t i = 0; i < n; ++i) ptrs[i] = new Node();
    if (shuffle_links) {
        mt19937 rng(20231117);
        shuffle(ptrs.begin(), ptrs.end(), rng);
    }
    for (size_t i = 0; i + 1 < n; ++i) ptrs[i]->next = ptrs[i + 1];
    ptrs[n - 1]->next = nullptr;
    return ptrs[0];
}

void free_list(Node* h) {
    while (h) { Node* t = h->next; delete h; h = t; }
}

// ---------- 实验1：顺序访问 ----------
void exp1_sequential() {
    string out = "/home/user/Doubao/chats/38454193854761474/exp1_seq.csv";
    vector<size_t> sizes = {1024, 4096, 16384, 65536, 100000};
    emit_row(out, {"size", "array_us", "list_us", "list_contig_us"}, true);
    for (size_t n : sizes) {
        vector<int> arr(n);
        mt19937 rng(7);
        for (auto& x : arr) x = (int)(rng() & 0x3F);
        // 数组：顺序遍历求和
        double t_arr = 1e18;
        for (int r = 0; r < 5; ++r) {
            t_arr = min(t_arr, time_us([&] {
                long long s = 0;
                for (size_t i = 0; i < n; ++i) s += arr[i];
                sink += s;
            }));
        }
        // 链表（节点随机分配、链接顺序随机）
        Node* h1 = build_list(n, true);
        double t_l1 = 1e18;
        for (int r = 0; r < 5; ++r) {
            t_l1 = min(t_l1, time_us([&] {
                long long s = 0;
                for (Node* p = h1; p; p = p->next) s += p->pad[0];
                sink += s;
            }));
        }
        free_list(h1);
        // 链表（节点随机分配、链接按分配顺序）：观察局部性影响
        Node* h2 = build_list(n, false);
        double t_l2 = 1e18;
        for (int r = 0; r < 5; ++r) {
            t_l2 = min(t_l2, time_us([&] {
                long long s = 0;
                for (Node* p = h2; p; p = p->next) s += p->pad[0];
                sink += s;
            }));
        }
        free_list(h2);
        emit_row(out, {"size", to_string(n), "array_us", to_string(t_arr),
                       "list_us", to_string(t_l1), "list_contig_us", to_string(t_l2)});
        cout << "exp1 n=" << n << " arr=" << t_arr << "us list=" << t_l1 << "us\n" << flush;
    }
}

// ---------- 实验2：随机访问 ----------
void exp2_random() {
    string out = "/home/user/Doubao/chats/38454193854761474/exp2_rand.csv";
    vector<size_t> sizes = {1024, 4096, 16384, 65536, 100000};
    emit_row(out, {"size", "array_us", "list_us"}, true);
    for (size_t n : sizes) {
        vector<int> arr(n);
        mt19937 rng(9);
        for (auto& x : arr) x = (int)(rng() & 0x3F);
        vector<size_t> idx(n);
        iota(idx.begin(), idx.end(), 0);
        shuffle(idx.begin(), idx.end(), rng);  // 预先打乱的随机访问序列
        // 数组：按随机索引访问
        double t_arr = 1e18;
        for (int r = 0; r < 3; ++r) {
            t_arr = min(t_arr, time_us([&] {
                long long s = 0;
                for (size_t i = 0; i < n; ++i) s += arr[idx[i]];
                sink += s;
            }));
        }
        // 链表：链接随机打乱，指针访问本身即随机跳转
        Node* h = build_list(n, true);
        double t_l = 1e18;
        for (int r = 0; r < 3; ++r) {
            t_l = min(t_l, time_us([&] {
                long long s = 0;
                for (Node* p = h; p; p = p->next) s += p->pad[0];
                sink += s;
            }));
        }
        free_list(h);
        emit_row(out, {"size", to_string(n), "array_us", to_string(t_arr),
                       "list_us", to_string(t_l)});
        cout << "exp2 n=" << n << " arr=" << t_arr << "us list=" << t_l << "us\n" << flush;
    }
}

// ---------- 实验3：LRU 缓存实现对比 ----------
class ArrayLRU {
    int cap;
    vector<int> keys;
public:
    explicit ArrayLRU(int c) : cap(c) {}
    bool access(int key) {
        auto it = find(keys.begin(), keys.end(), key);
        if (it == keys.end()) {          // 未命中：插入
            if ((int)keys.size() == cap) keys.pop_back();  // 淘汰最久未用（尾部）
            keys.insert(keys.begin(), key);
            return false;
        }
        keys.erase(it);                  // 命中：移动到头部
        keys.insert(keys.begin(), key);
        return true;
    }
};

class ListLRU {  // 双向链表 + 哈希表，O(1)
    int cap;
    list<int> l;
    unordered_map<int, list<int>::iterator> m;
public:
    explicit ListLRU(int c) : cap(c) {}
    bool access(int key) {
        auto it = m.find(key);
        if (it == m.end()) {
            if ((int)l.size() == cap) { m.erase(l.back()); l.pop_back(); }
            l.push_front(key);
            m[key] = l.begin();
            return false;
        }
        l.splice(l.begin(), l, it->second);  // 命中：移到头部
        return true;
    }
};

void exp3_lru() {
    string out = "/home/user/Doubao/chats/38454193854761474/exp3_lru.csv";
    emit_row(out, {"capacity", "ops", "array_us", "list_us", "array_hits", "list_hits"}, true);
    vector<int> caps = {16, 64, 128};
    const int OPS = 100000;          // 1e5 次访问
    const int KEYSPACE = 65536;
    for (int cap : caps) {
        // Zipf 风格热数据负载：90% 访问集中在 10% 的 key 范围
        vector<int> trace(OPS);
        mt19937 rng(42);
        for (int i = 0; i < OPS; ++i) {
            int hot = KEYSPACE / 10;
            trace[i] = (int)(rng() % 10 < 9) ? (int)(rng() % (uint32_t)hot)
                                             : (int)(rng() % (uint32_t)KEYSPACE);
        }
        ArrayLRU a(cap);
        long long ha = 0;
        double ta = time_us([&] {
            for (int k : trace) ha += a.access(k);
        });
        ListLRU l(cap);
        long long hl = 0;
        double tl = time_us([&] {
            for (int k : trace) hl += l.access(k);
        });
        sink += ha + hl;
        emit_row(out, {"capacity", to_string(cap), "ops", to_string(OPS),
                       "array_us", to_string(ta), "list_us", to_string(tl),
                       "array_hits", to_string(ha), "list_hits", to_string(hl)});
        cout << "exp3 cap=" << cap << " array=" << ta << "us list=" << tl
             << "us hitsA=" << ha << " hitsL=" << hl << "\n" << flush;
    }
}

int main() {
    exp1_sequential();
    exp2_random();
    exp3_lru();
    cout << "sink=" << sink << "\n";
    return 0;
}
