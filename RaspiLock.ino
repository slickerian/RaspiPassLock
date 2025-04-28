#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <WebServer.h>

// SD card pins
#define SD_CS    17
#define SD_SCK   18
#define SD_MOSI  19
#define SD_MISO  16

// WiFi settings
const char* apSSID = "PicoPasswordManager";
const char* apPassword = "secure123";
IPAddress apIP(192, 168, 4, 1);

// XOR encryption key (WARNING: XOR is weak; use stronger encryption for production)
const char* xorKey = "mysecretkey123456789";

// File to store user credentials (username:encrypted_master)
const char* usersFile = "/users.txt";

// Web server on port 80
WebServer server(80);

// Track current user and session
String currentUser = "";
unsigned long lastActivity = 0;
const unsigned long timeout = 300000; // 5 minutes

// Sanitize username to prevent file system issues
String sanitizeUsername(String username) {
  String result = "";
  for (size_t i = 0; i < username.length(); i++) {
    char c = username[i];
    if (isAlphaNumeric(c)) result += c; // Allow only alphanumeric
  }
  return result;
}

String getUserPasswordFile() {
  if (currentUser == "") return "";
  String path = "/" + sanitizeUsername(currentUser) + ".txt";
  Serial.print("Generated user file path: ");
  Serial.println(path);
  return path;
}

String xorEncryptDecrypt(String data, const char* key) {
  String result = "";
  size_t keyLen = strlen(key);
  for (size_t i = 0; i < data.length(); i++) {
    result += (char)(data[i] ^ key[i % keyLen]);
  }
  return result;
}

void initUsers() {
  if (!SD.exists(usersFile)) {
    // Create default user: admin/admin123
    String defaultMaster = "admin123";
    String encryptedMaster = xorEncryptDecrypt(defaultMaster, xorKey);
    File file = SD.open(usersFile, FILE_WRITE);
    if (file) {
      file.print("admin:");
      file.println(encryptedMaster);
      file.close();
      Serial.println("Default user 'admin' created in /users.txt");
    } else {
      Serial.println("Failed to create /users.txt");
    }
  }
  // Debug: Read and print /users.txt content
  File file = SD.open(usersFile, FILE_READ);
  if (file) {
    Serial.println("Content of /users.txt:");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      Serial.println(line);
    }
    file.close();
  } else {
    Serial.println("Failed to read /users.txt during init");
  }
}

void testSDWrite() {
  File testFile = SD.open("/test.txt", FILE_WRITE);
  if (testFile) {
    testFile.println("Test write at " + String(millis()));
    testFile.close();
    Serial.println("SD write test successful: /test.txt created");
  } else {
    Serial.println("SD write test failed: Could not write to /test.txt");
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Pico Password Manager</title>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Roboto:wght@400;700&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += "body { font-family: 'Roboto', sans-serif; margin: 0; padding: 20px; background: linear-gradient(to bottom, #e0f7fa, #ffffff); }";
  html += "h1 { color: #007bff; text-align: center; }";
  html += "h2 { color: #343a40; }";
  html += ".container { max-width: 800px; margin: auto; }";
  html += "form { background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }";
  html += "input[type='text'], input[type='password'] { width: 100%; padding: 10px; margin: 8px 0; border: 1px solid #ced4da; border-radius: 4px; box-sizing: border-box; }";
  html += "input:focus { border-color: #007bff; outline: none; box-shadow: 0 0 5px rgba(0,123,255,0.3); }";
  html += "button { padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; transition: transform 0.2s, background 0.2s; }";
  html += "button:hover { transform: scale(1.05); }";
  html += "button.primary { background: #28a745; color: white; }";
  html += "button.primary:hover { background: #218838; }";
  html += "button.delete { background: #dc3545; color: white; }";
  html += "button.delete:hover { background: #c82333; }";
  html += "button.neutral { background: #007bff; color: white; }";
  html += "button.neutral:hover { background: #0056b3; }";
  html += ".alert { padding: 10px; margin-bottom: 20px; border-radius: 4px; display: none; }";
  html += ".alert.success { background: #d4edda; color: #155724; }";
  html += ".alert.error { background: #f8d7da; color: #721c24; }";
  html += "table { width: 100%; border-collapse: collapse; background: #fff; border-radius: 8px; overflow: hidden; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += "th, td { padding: 12px; text-align: left; }";
  html += "th { background: #007bff; color: white; }";
  html += "tr:nth-child(even) { background: #f8f9fa; }";
  html += "tr:hover { background: #e9ecef; }";
  html += ".password-cell { position: relative; }";
  html += ".toggle-password { background: #6c757d; color: white; padding: 5px 10px; border-radius: 4px; cursor: pointer; }";
  html += ".toggle-password:hover { background: #5a6268; }";
  html += "</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>Pico Password Manager</h1>";

  // Alerts for dynamic feedback
  String alertMessage = "";
  String alertType = "";
  if (server.arg("error") == "1") {
    alertMessage = "Invalid username or password.";
    alertType = "error";
  } else if (server.arg("error") == "2") {
    alertMessage = "Username already exists or invalid (min 3 chars).";
    alertType = "error";
  } else if (server.arg("error") == "3") {
    alertMessage = "Master password must be at least 8 characters with letters and numbers.";
    alertType = "error";
  } else if (server.arg("error") == "4") {
    alertMessage = "Failed to save password. Check SD card.";
    alertType = "error";
  } else if (server.arg("success") == "1") {
    alertMessage = "Password added successfully!";
    alertType = "success";
  } else if (server.arg("success") == "2") {
    alertMessage = "Password deleted successfully!";
    alertType = "success";
  }
  if (alertMessage != "") {
    html += "<div class='alert " + alertType + "' id='alert'>" + alertMessage + "</div>";
  }

  if (currentUser == "") {
    // Login form
    html += "<h2>Login</h2>";
    html += "<form action='/login' method='POST'>";
    html += "Username: <input type='text' name='username' placeholder='Enter username'><br>";
    html += "Master Password: <input type='password' name='master' placeholder='Enter password'><br>";
    html += "<button type='submit' class='primary'>Login</button></form>";

    // Register form
    html += "<h2>Register</h2>";
    html += "<form action='/register' method='POST'>";
    html += "Username: <input type='text' name='username' placeholder='At least 3 characters'><br>";
    html += "Master Password: <input type='password' name='master' placeholder='At least 8 characters'><br>";
    html += "<button type='submit' class='primary'>Register</button></form>";
  } else {
    // Add password form
    html += "<h2>Add Password (User: " + currentUser + ")</h2>";
    html += "<form action='/add' method='POST'>";
    html += "Service: <input type='text' name='service' placeholder='e.g., Email'><br>";
    html += "Username: <input type='text' name='username' placeholder='e.g., user@example.com'><br>";
    html += "Password: <input type='text' name='password' placeholder='Enter password'><br>";
    html += "<button type='submit' class='primary'>Add Password</button></form>";

    // Show user's passwords in table
    html += "<h2>Your Stored Passwords</h2>";
    String userFile = getUserPasswordFile();
    if (!SD.exists(userFile.c_str())) {
      html += "<p>No passwords stored yet.</p>";
    } else {
      File file = SD.open(userFile.c_str(), FILE_READ);
      if (file) {
        html += "<table><tr><th>Service</th><th>Username</th><th>Password</th><th>Action</th></tr>";
        int rowIndex = 0;
        while (file.available()) {
          String line = file.readStringUntil('\n');
          if (line.length() > 0) {
            String decrypted = xorEncryptDecrypt(line, xorKey);
            int sep1 = decrypted.indexOf(": ");
            int sep2 = decrypted.lastIndexOf(": ");
            if (sep1 != -1 && sep2 != -1 && sep2 > sep1) {
              String service = decrypted.substring(0, sep1);
              String username = decrypted.substring(sep1 + 2, sep2);
              String password = decrypted.substring(sep2 + 2);
              html += "<tr>";
              html += "<td>" + service + "</td>";
              html += "<td>" + username + "</td>";
              html += "<td class='password-cell' id='password-" + String(rowIndex) + "'>****";
              html += "<span class='toggle-password' onclick='togglePassword(" + String(rowIndex) + ", \"" + password + "\")'>Show</span></td>";
              html += "<td><form action='/delete' method='POST'>";
              html += "<input type='hidden' name='entry' value='" + line + "'>";
              html += "<button type='submit' class='delete'>Delete</button></form></td>";
              html += "</tr>";
              rowIndex++;
            }
          }
        }
        html += "</table>";
        file.close();
      } else {
        html += "<p>Error reading passwords.</p>";
      }
    }

    // Logout
    html += "<form action='/logout' method='POST'>";
    html += "<button type='submit' class='neutral'>Logout</button></form>";
  }
  html += "</div>";

  // JavaScript for interactivity
  html += "<script>";
  html += "function togglePassword(rowIndex, password) {";
  html += "  var cell = document.getElementById('password-' + rowIndex);";
  html += "  var toggle = cell.querySelector('.toggle-password');";
  html += "  if (cell.innerText.startsWith('****')) {";
  html += "    cell.innerHTML = password + '<span class=\"toggle-password\" onclick=\"togglePassword(' + rowIndex + ', \\'' + password + '\\')\">Hide</span>';";
  html += "  } else {";
  html += "    cell.innerHTML = '****<span class=\"toggle-password\" onclick=\"togglePassword(' + rowIndex + ', \\'' + password + '\\')\">Show</span>';";
  html += "  }";
  html += "}";
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  var alert = document.getElementById('alert');";
  html += "  if (alert) {";
  html += "    alert.style.display = 'block';";
  html += "    setTimeout(function() {";
  html += "      alert.style.opacity = '0';";
  html += "      setTimeout(function() { alert.style.display = 'none'; }, 500);";
  html += "    }, 5000);";
  html += "  }";
  html += "});";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleLogin() {
  lastActivity = millis();
  String username = sanitizeUsername(server.arg("username"));
  String master = server.arg("master");

  Serial.print("Login attempt for username: ");
  Serial.println(username);

  if (username.length() < 3 || master.length() < 8) {
    Serial.println("Invalid input: username < 3 or master < 8");
    server.sendHeader("Location", "/?error=1");
    server.send(303);
    return;
  }

  String encryptedInputMaster = xorEncryptDecrypt(master, xorKey);
  Serial.print("Encrypted input master: ");
  Serial.println(encryptedInputMaster);

  File file = SD.open(usersFile, FILE_READ);
  bool valid = false;
  if (file) {
    Serial.println("Reading /users.txt:");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim(); // Remove trailing newline or spaces
      Serial.print("Line: ");
      Serial.println(line);
      int sep = line.indexOf(':');
      if (sep != -1) {
        String storedUser = line.substring(0, sep);
        String storedEncryptedMaster = line.substring(sep + 1);
        storedEncryptedMaster.trim(); // Ensure no trailing characters
        Serial.print("Comparing with stored user: ");
        Serial.print(storedUser);
        Serial.print(", encrypted master: ");
        Serial.println(storedEncryptedMaster);
        if (storedUser == username && storedEncryptedMaster == encryptedInputMaster) {
          valid = true;
          Serial.println("Match found!");
          break;
        }
      }
    }
    file.close();
  } else {
    Serial.println("Failed to open /users.txt");
  }

  if (valid) {
    currentUser = username;
    Serial.println("Login successful");
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    Serial.println("Login failed: No match found");
    server.sendHeader("Location", "/?error=1");
    server.send(303);
  }
}

void handleRegister() {
  String username = sanitizeUsername(server.arg("username"));
  String master = server.arg("master");

  // Validate inputs
  if (username.length() < 3 || username.length() > 20) {
    Serial.println("Registration failed: Invalid username length");
    server.sendHeader("Location", "/?error=2");
    server.send(303);
    return;
  }
  if (master.length() < 8 || master.length() > 50) {
    Serial.println("Registration failed: Invalid master password length");
    server.sendHeader("Location", "/?error=3");
    server.send(303);
    return;
  }
  bool hasLetter = false, hasNumber = false;
  for (size_t i = 0; i < master.length(); i++) {
    if (isAlpha(master[i])) hasLetter = true;
    if (isDigit(master[i])) hasNumber = true;
  }
  if (!hasLetter || !hasNumber) {
    Serial.println("Registration failed: Master password lacks letters or numbers");
    server.sendHeader("Location", "/?error=3");
    server.send(303);
    return;
  }

  // Check if username exists
  File file = SD.open(usersFile, FILE_READ);
  bool exists = false;
  if (file) {
    while (file.available()) {
      String line = file.readStringUntil('\n');
      int sep = line.indexOf(':');
      if (sep != -1 && line.substring(0, sep) == username) {
        exists = true;
        break;
      }
    }
    file.close();
  }

  if (exists) {
    Serial.println("Registration failed: Username exists");
    server.sendHeader("Location", "/?error=2");
    server.send(303);
    return;
  }

  // Register new user
  String encryptedMaster = xorEncryptDecrypt(master, xorKey);
  Serial.print("Registering user: ");
  Serial.print(username);
  Serial.print(" with encrypted master: ");
  Serial.println(encryptedMaster);

  file = SD.open(usersFile, FILE_WRITE | O_APPEND);
  if (file) {
    file.print(username + ":");
    file.println(encryptedMaster);
    file.close();
    currentUser = username;
    lastActivity = millis();
    Serial.println("Registration successful");
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    Serial.println("Registration failed: Cannot write to /users.txt");
    server.sendHeader("Location", "/?error=2");
    server.send(303);
  }
}

void handleLogout() {
  currentUser = "";
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleAdd() {
  if (currentUser == "") {
    Serial.println("Add password failed: No user logged in");
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  lastActivity = millis();

  String service = server.arg("service");
  String username = server.arg("username");
  String password = server.arg("password");

  Serial.print("Attempting to add password for user: ");
  Serial.println(currentUser);
  Serial.print("Service: ");
  Serial.println(service);
  Serial.print("Username: ");
  Serial.println(username);
  Serial.print("Password: ");
  Serial.println(password);

  // Relaxed validation: only password is required
  if (password == "") {
    Serial.println("Add password failed: Password is required");
    server.sendHeader("Location", "/?error=4");
    server.send(303);
    return;
  }
  if (service.length() > 50 || username.length() > 50 || password.length() > 50) {
    Serial.println("Add password failed: Input too long");
    server.sendHeader("Location", "/?error=4");
    server.send(303);
    return;
  }

  String entry = service + ": " + username + ": " + password;
  String encrypted = xorEncryptDecrypt(entry, xorKey);
  Serial.print("Encrypted entry: ");
  Serial.println(encrypted);

  String userFile = getUserPasswordFile();
  Serial.print("Writing to file: ");
  Serial.println(userFile);

  File file = SD.open(userFile.c_str(), SD.exists(userFile.c_str()) ? (FILE_WRITE | O_APPEND) : FILE_WRITE);
  if (file) {
    file.println(encrypted);
    file.close();
    Serial.println("Password added successfully for " + currentUser);
    // Debug: Read back file content
    File readBack = SD.open(userFile.c_str(), FILE_READ);
    if (readBack) {
      Serial.println("Content of " + userFile + ":");
      while (readBack.available()) {
        String line = readBack.readStringUntil('\n');
        Serial.println(line);
      }
      readBack.close();
    } else {
      Serial.println("Failed to read back " + userFile);
    }
    server.sendHeader("Location", "/?success=1");
    server.send(303);
  } else {
    Serial.println("Add password failed: Cannot open/write to " + userFile);
    server.sendHeader("Location", "/?error=4");
    server.send(303);
  }
}

void handleDelete() {
  if (currentUser == "") {
    Serial.println("Delete password failed: No user logged in");
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  lastActivity = millis();

  String entryToDelete = server.arg("entry");
  Serial.print("Attempting to delete entry: ");
  Serial.println(entryToDelete);

  String userFile = getUserPasswordFile();
  File tempFile = SD.open("/temp.txt", FILE_WRITE);
  File file = SD.open(userFile.c_str(), FILE_READ);
  if (tempFile && file) {
    while (file.available()) {
      String line = file.readStringUntil('\n');
      if (line != entryToDelete) {
        tempFile.println(line);
      }
    }
    file.close();
    tempFile.close();
    SD.remove(userFile.c_str());
    SD.rename("/temp.txt", userFile.c_str());
    Serial.println("Password deleted successfully for " + currentUser);
    server.sendHeader("Location", "/?success=2");
    server.send(303);
  } else {
    Serial.println("Delete password failed: File error");
    server.sendHeader("Location", "/?error=4");
    server.send(303);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin();

  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed! Halting...");
    while (true) delay(1000);
  }
  Serial.println("SD card initialized.");

  testSDWrite(); // Test SD card write capability
  initUsers();

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
    Serial.println("AP configuration failed! Halting...");
    while (true) delay(1000);
  }
  if (!WiFi.softAP(apSSID, apPassword)) {
    Serial.println("AP setup failed! Halting...");
    while (true) delay(1000);
  }
  Serial.println("Access Point started");
  Serial.print("IP Address: ");
  Serial.println(apIP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/register", HTTP_POST, handleRegister);
  server.on("/logout", HTTP_POST, handleLogout);
  server.on("/add", HTTP_POST, handleAdd);
  server.on("/delete", HTTP_POST, handleDelete);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
  if (currentUser != "" && millis() - lastActivity > timeout) {
    Serial.println("Session timed out for " + currentUser);
    currentUser = "";
  }
}