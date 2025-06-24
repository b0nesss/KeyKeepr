<!-- uses: libsodium, dear ImGui, sqlite3 -->
# KeyKeepr

KeyKeepr is a secure and user-friendly password manager built with C++ using [libsodium](https://libsodium.gitbook.io/doc/), [dear ImGui](https://github.com/ocornut/imgui), and [sqlite3](https://www.sqlite.org/index.html). It provides strong encryption, a modern graphical interface, and reliable local storage.

## Features

- **Strong Encryption:** All sensitive data is encrypted using libsodium.
- **Modern UI:** Clean and intuitive interface powered by dear ImGui.
- **Local Storage:** Passwords are stored securely in a local sqlite3 database.
- **Cross-Platform:** Runs on Windows, Linux, and macOS.
- **Lightweight:** Minimal dependencies and fast startup.

## Installation

1. **Clone the repository:**
    ```bash
    git clone https://github.com/b0nesss/KeyKeepr.git
    cd KeyKeepr
    ```

2. **Install dependencies:**
    - libsodium
    - dear ImGui
    - sqlite3
    - pkg-config

    On Ubuntu:
    ```bash
    sudo apt-get install libsodium-dev libsqlite3-dev pkg-config
    git clone https://github.com/ocornut/imgui
    ```

3. **Build the project:**
    ```bash
    mkdir build && cd build
    make
    ```

## Usage

Run the application:
```bash
./pass
```

Then create your own master password by first typing "hellohello" as the master password and then pressing the change master password button. Then follow the on screen instructions provided in the screenshots to generate your own passwords or save your own passwords.

## Screenshots
**Basic UI:**
![KeyKeepr UI](/ss/img1.png)
**Generating random password:**
![KeyKeepr UI](/ss/img2.png)
![KeyKeepr UI](/ss/img3.png)
**Getting a stored password:**
![KeyKeepr UI](/ss/img4.png)
![KeyKeepr UI](/ss/img5.png)
**Deleting a stored password:**
![KeyKeepr UI](/ss/img6.png)
![KeyKeepr UI](/ss/img7.png)
![KeyKeepr UI](/ss/img8.png)


## Security

- All passwords are encrypted at rest using libsodium.
- Master password is never stored.
- Database file is protected with strong cryptography.

##
**Happy Coding!!**