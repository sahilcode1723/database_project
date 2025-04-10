#include <bits/stdc++.h>
#include "json.hpp"
#include <sstream>
using json = nlohmann::json;
using namespace std;

const int default_ttl_seconds = 1800;

void to_json(json &j, const pair<string, time_t> &p)
{
    j = json{{"value", p.first}, {"expire_time", p.second}};
}

void from_json(const json &j, pair<string, time_t> &p)
{
    p.first = j.at("value").get<string>();
    p.second = j.at("expire_time").get<time_t>();
}

class KeyValueDB
{
private:
    enum ActionType { SET, DELETE };

    struct Action {
        ActionType type;
        string key;
        string oldValue;
        time_t oldTTL;
        string newValue;
        time_t newTTL;
    };

    unordered_map<string, pair<string, time_t>> store;
    map<int, unordered_map<string, pair<string, time_t>>> snapshots;
    int snapshot_id = 0;

    stack<Action> undoStack;
    stack<Action> redoStack;

    vector<string> auditLog;

    void logAction(const string &action)
    {
        time_t now = time(0);
        string timestamp = ctime(&now);
        timestamp.pop_back();
        auditLog.push_back("[" + timestamp + "] " + action);
    }

public:
    void set(string key, string value, int ttl = default_ttl_seconds)
    {
        time_t current_time = time(0);
        time_t expire_time = (ttl > 0) ? current_time + ttl : 0;

        string oldValue = store.count(key) ? store[key].first : "";
        time_t oldTTL = store.count(key) ? store[key].second : 0;

        undoStack.push({SET, key, oldValue, oldTTL, value, expire_time});
        redoStack = stack<Action>();

        store[key] = make_pair(value, expire_time);

        logAction("SET key: " + key + " value: " + value + " ttl: " + to_string(ttl));
        cout << "Key '" << key << "' set with TTL of " << ttl << " seconds\n";
    }

    string get(string key)
    {
        if (store.find(key) == store.end())
        {
            return "Key not found";
        }

        time_t current_time = time(0);
        string value = store[key].first;
        time_t expire_time = store[key].second;

        if (expire_time != 0 && current_time > expire_time)
        {
            store.erase(key);
            return "Key expired";
        }

        return value;
    }

    void del(const string &key)
    {
        if (store.find(key) != store.end())
        {
            string val = store[key].first;
            time_t ttl = store[key].second;
            undoStack.push({DELETE, key, val, ttl, "", 0});
            redoStack = stack<Action>();
            store.erase(key);

            logAction("DELETE key: " + key);
            cout << "Key '" << key << "' deleted.\n";
        }
        else
        {
            cout << "Key not found.\n";
        }
    }

    void undo()
    {
        if (undoStack.empty())
        {
            cout << "Nothing to undo.\n";
            return;
        }

        Action action = undoStack.top(); undoStack.pop();
        redoStack.push(action);

        if (action.type == SET)
        {
            if (action.oldValue == "")
            {
                store.erase(action.key);
            }
            else
            {
                store[action.key] = {action.oldValue, action.oldTTL};
            }
        }
        else if (action.type == DELETE)
        {
            store[action.key] = {action.oldValue, action.oldTTL};
        }

        logAction("UNDO action on key: " + action.key);
        cout << "Undo performed.\n";
    }

    void redo()
    {
        if (redoStack.empty())
        {
            cout << "Nothing to redo.\n";
            return;
        }

        Action action = redoStack.top(); redoStack.pop();
        undoStack.push(action);

        if (action.type == SET)
        {
            store[action.key] = {action.newValue, action.newTTL};
        }
        else if (action.type == DELETE)
        {
            store.erase(action.key);
        }

        logAction("REDO action on key: " + action.key);
        cout << "Redo performed.\n";
    }

    void cleanupExpiredKeys()
    {
        time_t current_time = time(0);
        for (auto it = store.begin(); it != store.end();)
        {
            if (it->second.second != 0 && current_time > it->second.second)
            {
                it = store.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void printStore()
    {
        cleanupExpiredKeys();
        cout << "\nCurrent Store:\n";
        if (store.empty())
        {
            cout << "(empty)\n";
        }
        for (const auto &pr : store)
        {
            cout << pr.first << ": " << pr.second.first << '\n';
        }
    }

    int snapshot()
    {
        cleanupExpiredKeys();
        snapshot_id++;
        snapshots[snapshot_id] = store;

        logAction("SNAPSHOT created with ID: " + to_string(snapshot_id));
        cout << "Snapshot created with ID: " << snapshot_id << "\n";
        return snapshot_id;
    }

    void listSnapshots() const
    {
        cout << "\nAvailable Snapshots:\n";
        if (snapshots.empty())
        {
            cout << "(no snapshots)\n";
        }
        for (const auto &snap : snapshots)
        {
            cout << "Snapshot ID: " << snap.first << "\n";
            for (const auto &pr : snap.second)
            {
                cout << pr.first << ": " << pr.second.first << '\n';
            }
        }
    }

    void restore(int id)
    {
        if (snapshots.find(id) != snapshots.end())
        {
            store = snapshots.at(id);
            undoStack = stack<Action>();
            redoStack = stack<Action>();

            logAction("RESTORE snapshot ID: " + to_string(id));
            cout << "Snapshot " << id << " restored successfully.\n";
        }
        else
        {
            cout << "Snapshot not found!\n";
        }
    }

    json toJSON()
    {
        json j;
        j["store"] = store;
        j["snapshot_id"] = snapshot_id;
        j["snapshots"] = snapshots;
        return j;
    }

    void saveToFile(const string &filename)
    {
        ofstream out(filename);
        if (!out)
        {
            cerr << "Error opening file for writing.\n";
            return;
        }
        try
        {
            json j = toJSON();
            out << j.dump(4);
            logAction("SAVE to file: " + filename);
            cout << "Database saved to " << filename << "\n";
        }
        catch (const json::exception &e)
        {
            cerr << "Failed to save database: " << e.what() << '\n';
        }
        out.close();
    }

    void loadFromFile(const string &filename)
    {
        ifstream in(filename);
        if (!in || in.peek() == ifstream::traits_type::eof())
        {
            cerr << "No valid previous database found. Starting fresh.\n";
            return;
        }
        try
        {
            json j;
            in >> j;
            store = j["store"].get<unordered_map<string, pair<string, time_t>>>();
            snapshot_id = j["snapshot_id"].get<int>();
            snapshots = j["snapshots"].get<map<int, unordered_map<string, pair<string, time_t>>>>();
            logAction("LOAD from file: " + filename);
            cout << "Database loaded from " << filename << "\n";
        }
        catch (const json::parse_error &e)
        {
            cerr << "Failed to parse database file: " << e.what() << '\n';
        }
        catch (const json::exception &e)
        {
            cerr << "JSON error: " << e.what() << '\n';
        }
    }

    void showAuditLog() const
    {
        cout << "\nAudit Log:\n";
        if (auditLog.empty())
        {
            cout << "(No actions logged yet)\n";
            return;
        }
        for (const auto &entry : auditLog)
        {
            cout << entry << '\n';
        }
    }
};

int main()
{
    KeyValueDB db;
    string line;

    cout << "Welcome to Key-Value DB CLI. Type 'help' for commands or 'exit' to quit.\n";

    while (true)
    {
        cout << "> ";
        getline(cin, line);
        stringstream ss(line);

        string command;
        ss >> command;

        if (command == "exit")
        {
            break;
        }
        else if (command == "set")
        {
            string key, value;
            ss >> key >> value;
            db.set(key, value);
        }
        else if (command == "setttl")
        {
            string key, value;
            int ttl;
            ss >> key >> value >> ttl;
            if (ss.fail())
            {
                cout << "Usage: setttl <key> <value> <ttl>\n";
            }
            else
            {
                db.set(key, value, ttl);
            }
        }
        else if (command == "get")
        {
            string key;
            ss >> key;
            cout << db.get(key) << endl;
        }
        else if (command == "del")
        {
            string key;
            ss >> key;
            db.del(key);
        }
        else if (command == "undo")
        {
            db.undo();
        }
        else if (command == "redo")
        {
            db.redo();
        }
        else if (command == "snapshot")
        {
            int id = db.snapshot();
            cout << "Snapshot ID: " << id << endl;
        }
        else if (command == "restore")
        {
            int id;
            ss >> id;
            db.restore(id);
        }
        else if (command == "save")
        {
            string filename;
            ss >> filename;
            db.saveToFile(filename);
        }
        else if (command == "load")
        {
            string filename;
            ss >> filename;
            db.loadFromFile(filename);
        }
        else if (command == "listSnapshots")
        {
            db.listSnapshots();
        }
        else if (command == "printStore")
        {
            db.printStore();
        }
        else if (command == "audit")
        {
            db.showAuditLog();
        }
        else if (command == "help")
        {
            cout << "Available commands:\n";
            cout << " set <key> <value>\n";
            cout << " setttl <key> <value> <ttl>\n";
            cout << " get <key>\n";
            cout << " del <key>\n";
            cout << " undo\n";
            cout << " redo\n";
            cout << " snapshot\n";
            cout << " restore <id>\n";
            cout << " save <filename>\n";
            cout << " load <filename>\n";
            cout << " listSnapshots\n";
            cout << " printStore\n";
            cout << " audit\n";
            cout << " exit\n";
        }
        else
        {
            cout << "Unknown command\n";
        }
    }

    return 0;
}
