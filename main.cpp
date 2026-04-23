
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <map>
#include <list>

using namespace std;

const int MAX_INDEX_LEN = 64;

struct Key {
    char index[MAX_INDEX_LEN + 1];
    int value;

    Key() {
        memset(index, 0, sizeof(index));
        value = 0;
    }

    Key(const string& idx, int val) {
        memset(index, 0, sizeof(index));
        strncpy(index, idx.c_str(), MAX_INDEX_LEN);
        value = val;
    }

    bool operator<(const Key& other) const {
        int cmp = strcmp(index, other.index);
        if (cmp != 0) return cmp < 0;
        return value < other.value;
    }

    bool operator==(const Key& other) const {
        return value == other.value && strcmp(index, other.index) == 0;
    }

    bool operator<=(const Key& other) const {
        return *this < other || *this == other;
    }
};

const int M = 200; // Degree of B+ Tree

struct Node {
    bool is_leaf;
    int size;
    Key keys[M];
    long children[M + 1]; // For internal nodes: offsets to children. For leaf nodes: children[0] is next leaf offset.
    long self_offset;

    Node() : is_leaf(true), size(0), self_offset(-1) {
        for (int i = 0; i <= M; ++i) children[i] = -1;
    }
};

class BPlusTree {
    string filename;
    fstream file;
    long root_offset;
    long end_offset;

    // Simple LRU Cache
    static const int CACHE_SIZE = 1000;
    map<long, list<pair<long, Node>>::iterator> cache_map;
    list<pair<long, Node>> cache_list;

    void read_node(Node& node, long offset) {
        auto it = cache_map.find(offset);
        if (it != cache_map.end()) {
            node = it->second->second;
            cache_list.splice(cache_list.begin(), cache_list, it->second);
            return;
        }

        file.seekg(offset);
        file.read(reinterpret_cast<char*>(&node), sizeof(Node));

        if (cache_list.size() >= CACHE_SIZE) {
            auto last = cache_list.back();
            cache_map.erase(last.first);
            cache_list.pop_back();
        }
        cache_list.push_front({offset, node});
        cache_map[offset] = cache_list.begin();
    }

    void write_node(const Node& node, long offset) {
        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(&node), sizeof(Node));

        auto it = cache_map.find(offset);
        if (it != cache_map.end()) {
            it->second->second = node;
            cache_list.splice(cache_list.begin(), cache_list, it->second);
        } else {
            if (cache_list.size() >= CACHE_SIZE) {
                auto last = cache_list.back();
                cache_map.erase(last.first);
                cache_list.pop_back();
            }
            cache_list.push_front({offset, node});
            cache_map[offset] = cache_list.begin();
        }
    }

    long allocate_node() {
        long offset = end_offset;
        end_offset += sizeof(Node);
        return offset;
    }

    void update_header() {
        file.seekp(0);
        file.write(reinterpret_cast<char*>(&root_offset), sizeof(long));
        file.write(reinterpret_cast<char*>(&end_offset), sizeof(long));
    }

public:
    BPlusTree(string fname) : filename(fname) {
        file.open(filename, ios::in | ios::out | ios::binary);
        if (!file) {
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);
            root_offset = -1;
            end_offset = 2 * sizeof(long);
            update_header();
        } else {
            file.seekg(0);
            file.read(reinterpret_cast<char*>(&root_offset), sizeof(long));
            file.read(reinterpret_cast<char*>(&end_offset), sizeof(long));
        }
    }

    ~BPlusTree() {
        update_header();
        file.close();
    }

    void insert(const string& index, int value) {
        Key key(index, value);
        if (root_offset == -1) {
            Node root;
            root.is_leaf = true;
            root.size = 1;
            root.keys[0] = key;
            root_offset = allocate_node();
            root.self_offset = root_offset;
            write_node(root, root_offset);
            update_header();
            return;
        }

        long new_child_offset = -1;
        Key new_child_key;
        insert_recursive(root_offset, key, new_child_offset, new_child_key);

        if (new_child_offset != -1) {
            Node new_root;
            new_root.is_leaf = false;
            new_root.size = 1;
            new_root.keys[0] = new_child_key;
            new_root.children[0] = root_offset;
            new_root.children[1] = new_child_offset;
            root_offset = allocate_node();
            new_root.self_offset = root_offset;
            write_node(new_root, root_offset);
            update_header();
        }
    }

    void insert_recursive(long current_offset, const Key& key, long& new_child_offset, Key& new_child_key) {
        Node node;
        read_node(node, current_offset);

        if (node.is_leaf) {
            int pos = lower_bound(node.keys, node.keys + node.size, key) - node.keys;
            if (pos < node.size && node.keys[pos] == key) return;

            for (int i = node.size; i > pos; --i) {
                node.keys[i] = node.keys[i - 1];
            }
            node.keys[pos] = key;
            node.size++;

            if (node.size < M) {
                write_node(node, current_offset);
                new_child_offset = -1;
            } else {
                Node sibling;
                sibling.is_leaf = true;
                int mid = M / 2;
                sibling.size = node.size - mid;
                for (int i = 0; i < sibling.size; ++i) {
                    sibling.keys[i] = node.keys[mid + i];
                }
                node.size = mid;
                sibling.children[0] = node.children[0];
                long sibling_offset = allocate_node();
                sibling.self_offset = sibling_offset;
                node.children[0] = sibling_offset;
                write_node(node, current_offset);
                write_node(sibling, sibling_offset);
                new_child_offset = sibling_offset;
                new_child_key = sibling.keys[0];
            }
        } else {
            int pos = upper_bound(node.keys, node.keys + node.size, key) - node.keys;
            long child_new_child_offset = -1;
            Key child_new_child_key;
            insert_recursive(node.children[pos], key, child_new_child_offset, child_new_child_key);

            if (child_new_child_offset != -1) {
                for (int i = node.size; i > pos; --i) {
                    node.keys[i] = node.keys[i - 1];
                    node.children[i + 1] = node.children[i];
                }
                node.keys[pos] = child_new_child_key;
                node.children[pos + 1] = child_new_child_offset;
                node.size++;

                if (node.size < M) {
                    write_node(node, current_offset);
                    new_child_offset = -1;
                } else {
                    Node sibling;
                    sibling.is_leaf = false;
                    int mid = M / 2;
                    new_child_key = node.keys[mid];
                    sibling.size = node.size - mid - 1;
                    for (int i = 0; i < sibling.size; ++i) {
                        sibling.keys[i] = node.keys[mid + 1 + i];
                        sibling.children[i] = node.children[mid + 1 + i];
                    }
                    sibling.children[sibling.size] = node.children[node.size];
                    node.size = mid;
                    long sibling_offset = allocate_node();
                    sibling.self_offset = sibling_offset;
                    write_node(node, current_offset);
                    write_node(sibling, sibling_offset);
                    new_child_offset = sibling_offset;
                }
            }
        }
    }

    void remove(const string& index, int value) {
        if (root_offset == -1) return;
        Key key(index, value);
        remove_recursive(root_offset, key);
        Node root;
        read_node(root, root_offset);
        if (!root.is_leaf && root.size == 0) {
            root_offset = root.children[0];
            update_header();
        }
    }

    void remove_recursive(long current_offset, const Key& key) {
        Node node;
        read_node(node, current_offset);

        if (node.is_leaf) {
            int pos = lower_bound(node.keys, node.keys + node.size, key) - node.keys;
            if (pos < node.size && node.keys[pos] == key) {
                for (int i = pos; i < node.size - 1; ++i) {
                    node.keys[i] = node.keys[i + 1];
                }
                node.size--;
                write_node(node, current_offset);
            }
        } else {
            int pos = upper_bound(node.keys, node.keys + node.size, key) - node.keys;
            remove_recursive(node.children[pos], key);
            
            Node child;
            read_node(child, node.children[pos]);
            if (child.size == 0 && !child.is_leaf) {
                // This case should be handled by merging, but for now we just keep it simple.
            }
            
            if (pos < node.size) {
                Key min_key = get_min_key(node.children[pos + 1]);
                if (!(node.keys[pos] == min_key)) {
                    node.keys[pos] = min_key;
                    write_node(node, current_offset);
                }
            }
        }
    }

    Key get_min_key(long offset) {
        Node node;
        read_node(node, offset);
        if (node.is_leaf) return node.keys[0];
        return get_min_key(node.children[0]);
    }

    void find(const string& index) {
        if (root_offset == -1) {
            cout << "null" << endl;
            return;
        }

        long current = root_offset;
        Node node;
        while (true) {
            read_node(node, current);
            if (node.is_leaf) break;
            int pos = upper_bound(node.keys, node.keys + node.size, Key(index, -2147483648)) - node.keys;
            current = node.children[pos];
        }

        vector<int> results;
        bool finished = false;
        while (!finished) {
            for (int i = 0; i < node.size; ++i) {
                int cmp = strcmp(node.keys[i].index, index.c_str());
                if (cmp == 0) {
                    results.push_back(node.keys[i].value);
                } else if (cmp > 0) {
                    finished = true;
                    break;
                }
            }
            if (finished || node.children[0] == -1) break;
            current = node.children[0];
            read_node(node, current);
        }

        if (results.empty()) {
            cout << "null" << endl;
        } else {
            sort(results.begin(), results.end());
            for (int i = 0; i < results.size(); ++i) {
                cout << results[i] << (i == results.size() - 1 ? "" : " ");
            }
            cout << endl;
        }
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    BPlusTree tree("data.db");

    int n;
    if (!(cin >> n)) return 0;

    string cmd, index;
    int value;
    for (int i = 0; i < n; ++i) {
        cin >> cmd;
        if (cmd == "insert") {
            cin >> index >> value;
            tree.insert(index, value);
        } else if (cmd == "delete") {
            cin >> index >> value;
            tree.remove(index, value);
        } else if (cmd == "find") {
            cin >> index;
            tree.find(index);
        }
    }

    return 0;
}
