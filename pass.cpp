#include <iostream>
#include <sodium/crypto_pwhash.h>
#include <sodium/randombytes.h>
#include <unordered_map>
#include <fstream>

#include <GLFW/glfw3.h>
#include <sodium.h>
#include <sqlite3.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#define BUFFER_LENGTH 256


typedef std::unordered_map<std::string, std::string> name_map;

const std::string alphabet = "abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "0123456789"
                             "!@#$%^&*()-_=+";

constexpr size_t SALT_BYTES = crypto_pwhash_SALTBYTES;
constexpr size_t NONCE_BYTES = crypto_secretbox_NONCEBYTES;
constexpr size_t KEY_BYTES = crypto_secretbox_KEYBYTES;

bool derive_key(const char * password, const unsigned char* salt, unsigned char* key_out) {
    return crypto_pwhash(key_out, KEY_BYTES, password, strlen(password),
                         salt, crypto_pwhash_OPSLIMIT_INTERACTIVE,
                         crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_DEFAULT) == 0;
}

// Encrypt the plaintext
std::string encrypt(const char *password, const char *master) {
    unsigned char salt[SALT_BYTES];
    unsigned char nonce[NONCE_BYTES];
    unsigned char key[KEY_BYTES];
    unsigned char out[strlen(password) + crypto_secretbox_MACBYTES];

    randombytes_buf(salt, sizeof salt);
    randombytes_buf(nonce, sizeof nonce);

    if (!derive_key(master, salt, key)) {
        std::cerr << "Key derivation failed.\n";
        return nullptr;
    }

    crypto_secretbox_easy(out,
                          reinterpret_cast<const unsigned char*>(password),
                          strlen(password), nonce, key);

    std::string res;
    res.append(reinterpret_cast<char *>(salt), SALT_BYTES);
    res.append(reinterpret_cast<char *>(nonce), NONCE_BYTES);
    res.append(reinterpret_cast<char *>(out), strlen(password) + crypto_secretbox_MACBYTES);
    return res;
}

bool decrypt(const char *password, const char *value, int len, char *decrypted_out) {
	const unsigned char *bytes = reinterpret_cast<const unsigned char *>(value);
    const unsigned char *salt = bytes;
    const unsigned char *nonce = &bytes[SALT_BYTES];
    const unsigned char *encrypt = &bytes[SALT_BYTES + NONCE_BYTES];

    unsigned char key[KEY_BYTES];
    if (!derive_key(password, salt, key)) {
        std::cerr << "Key derivation failed.\n";
        return false;
    }
	memset(decrypted_out, 0, BUFFER_LENGTH);
    int res = crypto_secretbox_open_easy(reinterpret_cast<unsigned char *>(decrypted_out),
                                         encrypt, len - SALT_BYTES - NONCE_BYTES, nonce, key);

    return true;
}

enum status {
    OK, NO_MASTER, WRONG_MASTER, INVALID_PASS, NO_NAME, ERROR
};

const char *get_status_from_enum(enum status status) {
    switch (status) {
    case WRONG_MASTER:
        return "Incorrect Master Password";
    case INVALID_PASS:
        return "Invalid Password";
    case NO_NAME:
        return "Requested name not in the list";
    case ERROR:
        return "Program encountered unknown error";
    case NO_MASTER:
        return "No Master password exists, first create one";
    default:
        return "OK";
    }
}

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}


void reencrypt_with_new_master(name_map& map, const char *old_master, const char *new_master) {
    for (auto& it : map) {
        char password[BUFFER_LENGTH];
        decrypt(old_master, it.second.c_str(), it.second.size(), password); 
        it.second = encrypt(password, new_master);
    }
}

bool check_master(const char *master, const char *hash) {
    return crypto_pwhash_str_verify(hash, master, strlen(master)) == 0;
}

void generate_random_password(char *buffer, int len) {
    randombytes_buf(buffer, len); 

    for (int i = 0; i < len; i++) {
        buffer[i] = alphabet[buffer[i] % alphabet.size()];
    }
    buffer[len + 1] = 0;
}

bool read_master(char *out, const char *source) {
    std::ifstream file(source, std::ios::in);
    if (!file.is_open()) {
        return false;
    }
    file.read(out, crypto_pwhash_STRBYTES);
    return true;
}

void write_master(char out[crypto_pwhash_STRBYTES], const char *dest) {
    std::ofstream file(dest, std::ios::out);
    file.write(out, crypto_pwhash_STRBYTES);
}


name_map read_database(sqlite3* db) {
    std::unordered_map<std::string, std::string> map;
    const char* sql = "SELECT key, value FROM kv;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Prepare failed: " << sqlite3_errmsg(db) << '\n';
        return map;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (key && value)
            map[key] = value;
    }

    sqlite3_finalize(stmt);
    return map;
}


void write_database(const name_map& map, sqlite3* db) {
    const char* sql = "REPLACE INTO kv (key, value) VALUES (?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Prepare failed: " << sqlite3_errmsg(db) << '\n';
        return;
    }

    for (const auto& it : map) {
        sqlite3_bind_text(stmt, 1, it.first.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, it.second.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Insert failed: " << sqlite3_errmsg(db) << '\n';
        }

        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
}

int main()
{
    sqlite3 *db;
    const char *master_file = "master_hash";

    char master_hash[crypto_pwhash_STRBYTES];
    char name_buffer[BUFFER_LENGTH];
    char password_buffer[BUFFER_LENGTH];
    char master_buffer[BUFFER_LENGTH];
    char tmp_master_buffer[BUFFER_LENGTH];

    memset(name_buffer, 0, BUFFER_LENGTH);
    memset(password_buffer, 0, BUFFER_LENGTH);
    memset(master_buffer, 0, BUFFER_LENGTH);

    bool changing_master = false;
    enum status status = OK;

    
    bool has_master = read_master(master_hash, master_file);


    if (sqlite3_open("vault.db", &db) != SQLITE_OK) {
        std::cerr << "Failed to open DB\n";
        return 1;
    }

    const char* create_table_sql = "CREATE TABLE IF NOT EXISTS kv (key TEXT PRIMARY KEY, value TEXT);";
    char* errmsg = nullptr;
    if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::cerr << "Create table failed: " << errmsg << '\n';
        sqlite3_free(errmsg);
        return 1;
    }

    name_map passwords = read_database(db);

    if (sodium_init() < 0) {
        std::cerr << "Failed to initialize libsodium\n";
        return 1;
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(800, 600, "pass", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    io.Fonts->AddFontFromFileTTF("DejaVuSans.ttf", 32.0f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::PushFontSize(32.0f);

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(io.DisplaySize);

		bool popen = true;

        ImGui::Begin("pass", &popen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration);

		ImGui::Text("Name");
        ImGui::SameLine(io.DisplaySize.x / 2);
        if (ImGui::Button("delete password")) {
            std::string target(name_buffer);
            if (passwords.find(target) == passwords.end()) {
                status = NO_NAME;
            } else {
                passwords.erase(target);
                memset(name_buffer, 0, BUFFER_LENGTH);
                status = OK;
            }
        }

		ImGui::PushItemWidth(io.DisplaySize.x - ImGui::GetFrameHeight() * 2); 
        if (ImGui::InputText("##name_buffer", name_buffer, IM_ARRAYSIZE(name_buffer))) {}
        ImGui::PopItemWidth();
        ImGui::SameLine();
		ImGui::PushItemWidth(io.DisplaySize.x - ImGui::GetStyle().FramePadding.x * 2); 
        if (ImGui::BeginCombo("##name_combo", "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {
            for (auto& it: passwords) {
                if(it.first.length()==0) continue;
                const char* str = it.first.c_str();
				bool selected = strcmp(name_buffer, str) == 0;
				if (ImGui::Selectable(str, selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    strcpy(name_buffer, str);

                    if (selected)
                        ImGui::SetItemDefaultFocus();
				}
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

		ImGui::Text("Password");
        ImGui::SameLine(io.DisplaySize.x / 2);
        if (ImGui::Button("random password")) {
            generate_random_password(password_buffer, 8);
            status = OK;
        }

		ImGui::PushItemWidth(io.DisplaySize.x - ImGui::GetStyle().FramePadding.x * 2); 
        ImGui::InputText("##password", password_buffer, IM_ARRAYSIZE(password_buffer));
        ImGui::PopItemWidth();

		ImGui::Text(changing_master ? "Enter new Master password" : "Master password");
		ImGui::PushItemWidth(io.DisplaySize.x - ImGui::GetStyle().FramePadding.x * 2); 
        ImGui::InputText("##master", master_buffer, IM_ARRAYSIZE(master_buffer));
        ImGui::PopItemWidth();

        if (ImGui::Button("add password")) {
            if (has_master) {
                if (check_master(master_buffer, master_hash)) {
                    std::string hash = encrypt(password_buffer, master_buffer);
                    passwords[name_buffer] = hash;
                    status = OK;
                } else {
                    status = WRONG_MASTER;
                }
            } else {
                status = NO_MASTER;
            }
            memset(password_buffer, 0, BUFFER_LENGTH);
            memset(master_buffer, 0, BUFFER_LENGTH);
        }

        ImGui::SameLine();

        if (ImGui::Button("get password")) {
            if (has_master) {
                auto hash = passwords.find(name_buffer);
                if (hash == passwords.end()) {
                    status = NO_NAME; 
                } else if (check_master(master_buffer, master_hash)) {
                    decrypt(master_buffer, hash->second.c_str(),
                            hash->second.size(), password_buffer);
                    status = OK;
                } else {
                    status = WRONG_MASTER;
                }
            } else {
                status = NO_MASTER;
            }
            memset(master_buffer, 0, BUFFER_LENGTH);
        }

        ImGui::SameLine();

        if (ImGui::Button("change master password")) {
            if (changing_master || !has_master) {
                if (crypto_pwhash_str(master_hash, master_buffer, strlen(master_buffer),
                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
                    status = ERROR;
                }
                write_master(master_hash, master_file);
                changing_master = false;
                reencrypt_with_new_master(passwords, tmp_master_buffer, master_buffer); 

                memset(master_buffer, 0, BUFFER_LENGTH);
                memset(tmp_master_buffer, 0, BUFFER_LENGTH);

                status = OK;
                if (!has_master)
                    has_master = true;
            } else {
                if (check_master(master_buffer, master_hash)) {
                    changing_master = true;
                    strcpy(tmp_master_buffer, master_buffer);
                    status = OK;
                } else {
                    status = WRONG_MASTER;
                }
            }
            memset(master_buffer, 0, BUFFER_LENGTH);
        }

        ImGui::SameLine();

        if (ImGui::Button("exit")) {
            glfwSetWindowShouldClose(window, 1);
        }

        ImGui::Text("%s", get_status_from_enum(status));


        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    write_database(passwords, db);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
