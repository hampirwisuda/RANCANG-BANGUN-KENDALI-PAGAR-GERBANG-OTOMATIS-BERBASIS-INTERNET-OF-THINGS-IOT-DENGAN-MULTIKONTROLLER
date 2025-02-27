#define BLYNK_TEMPLATE_ID "TMPL6tzqd-Nvs"
#define BLYNK_TEMPLATE_NAME "Notifikasi Pintu Gerbang"
#define BLYNK_AUTH_TOKEN "eKSPTwJ9W7T1U7J96-Ow-cdXbovyJj-c"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Ticker.h>
#include <NewPing.h>

// WiFi credentials
const char* ssid = "e-UnimalNet";
const char* password = "unimalnet";

// Pin konfigurasi untuk ESP32 dan peralatan terkait
#define RST_PIN 22        // Pin Reset untuk ESP32
#define SS_PIN 21         // Pin SS untuk ESP32

// Pin driver TB6600
#define ENA_PIN 25        // Enable pin
#define DIR_PIN 26        // Direction pin
#define PULL_PIN 27       // Step pin

// Pin untuk LED indikator
#define GREEN_LED_PIN 16  // LED Hijau (CW)
#define RED_LED_PIN 17    // LED Merah (CCW)
#define YELLOW_LED_PIN 12

// Pin untuk buzzer
#define BUZZER_PIN 5      // Pin buzzer

// Pin untuk relay
#define RELAY_PIN 4       // Pin relay

// Pin sensor magnet
#define MAGNET_SENSOR_OPEN_PIN 32  // Sensor magnet untuk pintu terbuka (CW)
#define MAGNET_SENSOR_CLOSE_PIN 33 // Sensor magnet untuk pintu tertutup (CCW)
#define MAX_DISTANCE 29 // Maksimum jarak (dalam cm)

#define TRIG_PIN 13   // Pin TRIG sensor ultrasonik
#define ECHO_PIN 14   // Pin ECHO sensor ultrasonik

// Inisialisasi pustaka NewPing
NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE);

// Objek MFRC522
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Timer untuk motor
Ticker motorTicker;

// UID yang valid untuk kartu
byte validUID[] = {0x13, 0x37, 0x3D, 0x34};

// Status sistem
bool motorActive = false;   // Status motor
bool doorOpen = false;      // Status pintu
bool direction = HIGH;      // Arah motor (CW atau CCW)
bool blynkConnected = false; // Status koneksi Blynk
bool isCardBlocked = false;  // Status blokir kartu
unsigned long lastSensorReadTime = 0;
unsigned long obstacleDetectedTime = 0;
bool obstacleDetected = false;
bool doorClosed = false;  // Menandai apakah pintu sudah tertutup
unsigned long waitStartTime = 0; // Waktu mulai tunggu saat magnet terbuka terdeteksi
bool waitingForRFID = false; // Status untuk memeriksa apakah kita sedang menunggu RFID
bool autoClosePending = false; // Status untuk memantau penutupan otomatis
bool autoCloseFinished = false; // Status untuk menandai bahwa penutupan otomatis telah selesai

// Variabel waktu
unsigned long lastScanTime = 0;
const unsigned long scanDelay = 1000;  // Delay antar scan kartu (1 detik)
unsigned long stepDelay = 400;        // Delay antar langkah motor (1 ms)
unsigned long lastStepTime = 0;       // Waktu terakhir langkah motor
unsigned long lastResetTime = 0;      // Waktu terakhir reset MFRC522
const unsigned long resetInterval = 5000; // Interval reset MFRC522 (5 detik)
unsigned long wifiReconnectInterval = 10000; // Interval reconnect WiFi (10 detik)
unsigned long lastWifiReconnectTime = 0;   // Waktu terakhir reconnect WiFi
unsigned long lastSyncTime = 0;  // Waktu terakhir sinkronisasi ke Blynk
const unsigned long syncInterval = 5000;  // Interval sinkronisasi (5 detik)
bool previousMotorState = true;  // Menyimpan status motor sebelumnya
unsigned long previousMillis = 0;  // Menyimpan waktu sebelum deteksi objek
const unsigned long interval = 1000;
const long sensorInterval = 100; // Interval pembacaan sensor dalam milidetik (100 ms)
unsigned long startTime = 0;  // Waktu awal deteksi objek
unsigned long lastObjectDetectedTime = 0; // Waktu terakhir objek terdeteksi

// Fungsi untuk koneksi WiFi dan Blynk
void manageWiFiAndBlynk() {
    // Periksa status WiFi
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi terputus. Menghubungkan kembali...");
        WiFi.begin(ssid, password);
        unsigned long startAttemptTime = millis();

        // Tunggu koneksi WiFi (timeout 10 detik)
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi berhasil terhubung!");
            Serial.print("Alamat IP: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("\nGagal terhubung ke WiFi. Akan mencoba lagi nanti.");
        }
    }

    // Periksa status koneksi Blynk
    if (WiFi.status() == WL_CONNECTED) {
        if (!Blynk.connected()) {
            Serial.println("Blynk terputus. Menghubungkan kembali...");
            if (Blynk.connect()) {
                Serial.println("Blynk berhasil terhubung!");
            } else {
                Serial.println("Gagal terhubung ke Blynk. Akan mencoba lagi nanti.");
            }
        } else {
            Blynk.run(); // Jalankan proses Blynk jika koneksi aktif
        }
    }
}

bool readCard() {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        bool isValid = true;
        
        // Jika kartu diblokir, langsung dianggap salah dan beep panjang
        if (isCardBlocked) {
            Serial.println("Kartu diblokir!");
            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();
            resetMFRC522();
            // Beep panjang selama 3 detik
            digitalWrite(BUZZER_PIN, HIGH);
            delay(3000);  // Durasi beep panjang
            digitalWrite(BUZZER_PIN, LOW);
            return false;
        }

        // Verifikasi UID kartu
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            if (mfrc522.uid.uidByte[i] != validUID[i]) {
                isValid = false;
                break;
            }
        }
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();

        // Reset MFRC522 setelah membaca kartu, terlepas dari validitasnya
        resetMFRC522();

        if (isValid) {
            Serial.println("Kunci Benar");
            // Bunyi buzzer dua kali
            for (int i = 0; i < 2; i++) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(200);  // Durasi bunyi buzzer
                digitalWrite(BUZZER_PIN, LOW);
                delay(200);  // Jeda antar bunyi buzzer
            }
            return true;
        } else {
            Serial.println("Kunci Salah");
            // Beep panjang selama 3 detik untuk kartu tidak valid
            digitalWrite(BUZZER_PIN, HIGH);
            delay(3000);  // Durasi beep panjang
            digitalWrite(BUZZER_PIN, LOW);
            return false;
        }
    }
    return false;
}

// Fungsi untuk mereset MFRC522
void resetMFRC522() {
    mfrc522.PCD_Reset();
    mfrc522.PCD_Init();
}


// Fungsi untuk mengontrol LED dan buzzer berkedip
void blinkLEDAndBuzzer() {
    if (motorActive) {
        if (direction == HIGH) {
            digitalWrite(GREEN_LED_PIN, millis() % 1000 < 500);  // LED hijau berkedip
            digitalWrite(RED_LED_PIN, LOW);                       // Matikan LED merah
            digitalWrite(BUZZER_PIN, millis() % 1000 < 500);      // Buzzer ikut berkedip
        } else {
            digitalWrite(RED_LED_PIN, millis() % 1000 < 500);    // LED merah berkedip
            digitalWrite(GREEN_LED_PIN, LOW);                      // Matikan LED hijau
            digitalWrite(BUZZER_PIN, millis() % 1000 < 500);      // Buzzer ikut berkedip
        }
    } else {
        digitalWrite(GREEN_LED_PIN, LOW);
        digitalWrite(RED_LED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);  // Matikan buzzer jika motor tidak aktif
    }
}

// Fungsi untuk langkah motor
void stepMotor() {
    if (motorActive) {
        if (micros() - lastStepTime >= stepDelay) {  // Periksa interval waktu antar langkah motor
            lastStepTime = micros();  // Update waktu langkah terakhir
            digitalWrite(PULL_PIN, HIGH);
            delayMicroseconds(10);  // Pulsa pendek untuk satu langkah
            digitalWrite(PULL_PIN, LOW);
        }
    }
}

// Fungsi untuk mengatur status relay
void updateRelayStatus() {
    if (doorOpen) {
        digitalWrite(RELAY_PIN, HIGH);  // Matikan relay jika pintu terbuka
        Serial.println("Kunci dibuka. Pintu dibuka.");
    } else {
        digitalWrite(RELAY_PIN, LOW);   // Nyalakan relay untuk mengunci pintu
        Serial.println("Pintu terkunci.");
        // Buzzer berkedip saat relay aktif
        for (int i = 0; i < 3; i++) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(200);
            digitalWrite(BUZZER_PIN, LOW);
            delay(200);
        }
    }
}

void checkCardBlockStatusAtStartup() {
    Serial.println("Mengecek status blokir kartu saat startup...");

    // Status blokir diatur berdasarkan nilai `isCardBlocked` yang diperbarui oleh fungsi BLYNK_WRITE(V2)
    if (isCardBlocked) {
        Serial.println("Kartu diblokir saat startup.");
    } else {
        Serial.println("Kartu unblokir saat startup.");
    }
}

// Fungsi untuk mengecek status pintu pada startup
void checkDoorStatusAtStartup() {
    Serial.println("Mengecek status pintu saat startup...");
    delay(3000); // Tunggu 3 detik agar sensor magnet stabil

    // Membaca status sensor magnet beberapa kali untuk memastikan
    bool openDetected = false;
    bool closeDetected = false;

    for (int i = 0; i < 5; i++) { // Baca 5 kali untuk memastikan
        if (digitalRead(MAGNET_SENSOR_OPEN_PIN) == LOW) {
            openDetected = true;
        }
        if (digitalRead(MAGNET_SENSOR_CLOSE_PIN) == LOW) {
            closeDetected = true;
        }
        delay(100); // Delay antar pembacaan untuk debounce
    }

    // Menentukan status pintu berdasarkan pembacaan sensor
    if (openDetected && !closeDetected) {
        Serial.println("Pintu terbuka saat startup. Menutup pintu...");
        doorOpen = true;     // Tandai pintu dalam keadaan terbuka
        direction = LOW;     // Siapkan motor untuk menutup
        digitalWrite(DIR_PIN, direction);
        motorActive = true;  // Aktifkan motor
        stepMotor();        // Fungsi untuk mulai menggerakkan motor
    } else if (closeDetected && !openDetected) {
        Serial.println("Pintu tertutup saat startup.");
        doorOpen = false;    // Tandai pintu dalam keadaan tertutup
        direction = LOW;     // Siapkan motor untuk membuka jika dibutuhkan
        digitalWrite(DIR_PIN, direction);
        motorActive = false; // Motor tidak perlu bergerak
    } else {
        // Jika status pintu tidak diketahui
        Serial.println("Status pintu tidak diketahui saat startup. Menutup pintu...");
        doorOpen = false;    // Asumsikan pintu tertutup untuk keamanan
        direction = LOW;     // Siapkan motor untuk menutup
        digitalWrite(DIR_PIN, direction);
        motorActive = true;  // Aktifkan motor
        stepMotor();        // Fungsi untuk mulai menggerakkan motor
    }

    // Perbarui status relay berdasarkan kondisi pintu
    updateRelayStatus();
    Serial.println("Status pintu pada startup diperbarui.");
}


void openGate() {
    Serial.println("Membuka pintu...");
    
    // Memastikan relay dimatikan terlebih dahulu
    doorOpen = true;  // Set status pintu terbuka
    updateRelayStatus();  // Mematikan relay jika pintu terbuka
    
    delay(1000);  // Tunggu 1 detik sebelum membuka pintu
    
    // Mengatur arah motor untuk membuka pintu
    direction = HIGH; // Arah motor CW
    digitalWrite(DIR_PIN, direction);
    
    motorActive = true;  // Mengaktifkan motor
    digitalWrite(ENA_PIN, LOW); // Menyalakan motor
    }


void closeGate() {
    Serial.println("Menutup pintu...");

    // Memastikan relay dimatikan terlebih dahulu sebelum menutup pintu
    doorOpen = false;  // Set status pintu tertutup
    updateRelayStatus();  // Mematikan relay jika pintu tertutup
    
    // Mengatur arah motor untuk menutup pintu
    direction = LOW; // Arah motor CCW
    digitalWrite(DIR_PIN, direction);
    
    motorActive = true;  // Mengaktifkan motor
    digitalWrite(ENA_PIN, LOW); // Menyalakan motor

    // Setelah pintu tertutup, nyalakan relay untuk mengunci pintu
    updateRelayStatus();  // Menyalakan relay untuk mengunci pintu
}


// Callback untuk Blynk Virtual Pin V1
BLYNK_WRITE(V1) {
    int value = param.asInt(); // Membaca nilai dari Blynk App
    
    // Jika tombol ditekan, buka atau tutup pintu hanya jika motor tidak aktif
    if (!motorActive) {
        if (value == 1) {
            openGate();  // Jika tombol ditekan untuk membuka pintu
        } else if (value == 0) {
            closeGate(); // Jika tombol ditekan untuk menutup pintu
        }
    }
}

// Fungsi untuk menangani perubahan status pin V2 dari Blynk
BLYNK_WRITE(V2) {
    int value = param.asInt(); // Membaca nilai dari Blynk App
    
    // Perbarui status blokir kartu sesuai dengan nilai dari Blynk
    if (value == 1) {
        isCardBlocked = true;  // Blokir kartu
        Serial.println("Kartu diblokir melalui Blynk.");
    } else if (value == 0) {
        isCardBlocked = false;  // Unblokir kartu
        Serial.println("Kartu unblokir melalui Blynk.");
    }
}

// Fungsi untuk sinkronisasi status pintu ke Blynk
void syncGateStatusToBlynk() {
    // Hanya update status tombol jika pintu tidak dalam kondisi bergerak
    if (!motorActive) {
        if (doorOpen) {
            Blynk.virtualWrite(V1, 1); // Pintu terbuka, kirim nilai 1
        } else {
            Blynk.virtualWrite(V1, 0); // Pintu tertutup, kirim nilai 0
        }
    }
}

long getDistance() {
    long distance = sonar.ping_cm();  // Mengukur jarak dalam cm

    // Hanya tampilkan hasil jika direction == LOW
    if (direction == LOW && distance > 2) {
        Serial.print("Jarak yang terdeteksi: ");
        Serial.println(distance);
    }

    // Jika jarak tidak valid (lebih kecil dari 2 cm atau lebih besar dari 29 cm), kembalikan -1
    if (distance < 2 || distance > 29) {
        return -1;  // Jarak tidak valid
    }
    
    return distance;  // Kembalikan jarak yang terukur
}

void antiKecepit() {
    long distance = getDistance(); // Membaca jarak dari sensor

    if (distance != -1 && distance <= 29) { // Objek terdeteksi
        if (motorActive) {
            motorActive = false; // Hentikan motor
            digitalWrite(YELLOW_LED_PIN, HIGH); // Nyalakan LED kuning
            digitalWrite(RED_LED_PIN, LOW); // Matikan LED merah
            digitalWrite(BUZZER_PIN, HIGH); // Nyalakan buzzer
            Serial.println("Objek terdeteksi! Motor dihentikan.");

            // Tunggu 5 detik untuk validasi ulang
            delay(5000);

            // Validasi ulang jarak setelah 5 detik
            distance = getDistance();
            if (distance != -1 && distance <= 29) {
                // Jika objek masih ada setelah 5 detik
                Serial.println("Objek masih ada setelah 5 detik. Motor tetap berhenti.");
                // Panggil fungsi ini lagi untuk memeriksa kondisi ulang
                antiKecepit(); // Memanggil fungsi ini kembali
            } else {
                // Jika objek sudah hilang setelah 5 detik
                motorActive = true; // Aktifkan kembali motor
                digitalWrite(YELLOW_LED_PIN, LOW); // Matikan LED kuning
                digitalWrite(BUZZER_PIN, LOW); // Matikan buzzer
                Serial.println("Objek hilang setelah validasi ulang. Motor melanjutkan.");
            }
        }
    } else {
        // Jika tidak ada objek dan motor tidak aktif
        if (!motorActive) {
            delay(6000);
            motorActive = true; // Aktifkan motor
            digitalWrite(YELLOW_LED_PIN, LOW); // Matikan LED kuning
            digitalWrite(RED_LED_PIN, HIGH); // Nyalakan LED merah
            digitalWrite(BUZZER_PIN, LOW); // Matikan buzzer
            Serial.println("Objek hilang, motor melanjutkan.");
        }
    }
}



void sendDoorOpenNotification() {
    Blynk.logEvent("buka", "Pintu Terbuka!");  // Kirim notifikasi pintu terbuka
}

void sendDoorCloseNotification() {
    Blynk.logEvent("tutup", "Pintu Tertutup!");  // Kirim notifikasi pintu tertutup
}

void setup() {
    Serial.begin(115200);

    // Inisialisasi perangkat keras
    SPI.begin(18, 19, 23, 21);
    mfrc522.PCD_Init();
    pinMode(ENA_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(PULL_PIN, OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(MAGNET_SENSOR_OPEN_PIN, INPUT_PULLUP);
    pinMode(MAGNET_SENSOR_CLOSE_PIN, INPUT_PULLUP);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(YELLOW_LED_PIN, OUTPUT);

    // Inisialisasi WiFi dan Blynk
    WiFi.begin(ssid, password);
    Blynk.config(BLYNK_AUTH_TOKEN);

    Serial.println("Menghubungkan ke WiFi...");
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi terhubung!");
        Serial.print("Alamat IP: ");
        Serial.println(WiFi.localIP());
        Blynk.connect(); // Koneksi ke Blynk

        // Tunggu beberapa saat agar koneksi ke server Blynk stabil
        delay(1000);
        Blynk.syncVirtual(V2); // Sinkronisasi status V2
    } else {
        Serial.println("\nGagal menghubungkan ke WiFi. Berjalan dalam mode offline.");
    }

    // Periksa status blokir kartu dengan nilai yang diperbarui dari Blynk
    checkCardBlockStatusAtStartup();
    motorTicker.attach_ms(stepDelay / 1000, stepMotor);
    checkDoorStatusAtStartup();
    digitalWrite(YELLOW_LED_PIN, LOW);

    Serial.println("TOLONG RFID DI SCAN BANG KALAU MAU MASUK!!!");
}


void loop() {
     unsigned long currentTime = millis();

    // Kelola koneksi WiFi dan Blynk
    if (WiFi.status() == WL_CONNECTED) {
        if (!Blynk.connected()) {
            Blynk.connect();  // Coba koneksi ulang ke Blynk
        }
        Blynk.run();  // Jalankan Blynk jika terhubung
    }

     // Periksa status koneksi Blynk dan sinkronkan hanya saat pertama kali terhubung
    if (Blynk.connected() && !blynkConnected) {
        blynkConnected = true; // Tandai Blynk sudah terhubung
        Blynk.syncVirtual(V2); // Sinkronisasi status pin V2
    } else if (!Blynk.connected() && blynkConnected) {
        blynkConnected = false; // Tandai Blynk terputus
    }

    // Perbarui status pin V2 jika diperlukan
    if (currentTime - lastSyncTime >= syncInterval) {
        syncGateStatusToBlynk();
        lastSyncTime = currentTime;  // Perbarui waktu sinkronisasi terakhir
    }

    // Reset MFRC522 jika sudah waktunya
    if (currentTime - lastResetTime > resetInterval) {
        resetMFRC522();
        lastResetTime = currentTime;
    }

    // Scan kartu RFID
    if (currentTime - lastScanTime > scanDelay) {
        if (readCard()) {
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println("Kunci dibuka. Pintu mulai membuka...");
            digitalWrite(BUZZER_PIN, HIGH);
            delay(500);
            digitalWrite(BUZZER_PIN, LOW);
            direction = !direction;
            digitalWrite(DIR_PIN, direction);
            motorActive = true;
            digitalWrite(ENA_PIN, HIGH);
            delay(2000);
            digitalWrite(ENA_PIN, LOW);

            // Reset status menunggu RFID setelah kartu terbaca
            waitingForRFID = false;
            waitStartTime = 0;
            autoClosePending = false; // Reset penutupan otomatis
            autoCloseFinished = false; // Reset flag penutupan otomatis selesai
        }
        lastScanTime = currentTime;
    }

    // Cek apakah magnet terbuka terdeteksi
    if (digitalRead(MAGNET_SENSOR_OPEN_PIN) == LOW && !waitingForRFID && !autoCloseFinished) {
        // Magnet terbuka terdeteksi, mulai waktu tunggu
        waitStartTime = currentTime;
        waitingForRFID = true;
        autoClosePending = true; // Aktifkan penutupan otomatis
        Serial.println("Magnet terbuka terdeteksi, menunggu RFID selama 15 detik...");
    }

    // Jika dalam waktu tunggu 15 detik, periksa apakah RFID terbaca
    if (waitingForRFID && (currentTime - waitStartTime < 15000)) {
        if (readCard()) {
            // Jika RFID terbaca dalam waktu 15 detik, tutup pintu
            Serial.println("RFID terdeteksi dalam waktu tunggu, pintu akan ditutup...");
            digitalWrite(RELAY_PIN, HIGH);
            direction = LOW;
            digitalWrite(DIR_PIN, direction);
            motorActive = true;
            digitalWrite(ENA_PIN, HIGH);
            delay(2000);
            digitalWrite(ENA_PIN, LOW);

            // Reset status menunggu RFID setelah pintu ditutup
            waitingForRFID = false;
            waitStartTime = 0;
            autoClosePending = false; // Nonaktifkan penutupan otomatis
            autoCloseFinished = true; // Tandai proses penutupan otomatis selesai
        }
    }

    // Jika sudah lewat 15 detik dan RFID belum terbaca, tutup pintu secara otomatis
    if (autoClosePending && (currentTime - waitStartTime >= 15000)) {
        Serial.println("Waktu tunggu 15 detik selesai tanpa RFID, pintu ditutup otomatis...");
        digitalWrite(RELAY_PIN, HIGH);
        direction = LOW;
        digitalWrite(DIR_PIN, direction);
        motorActive = true;
        digitalWrite(ENA_PIN, HIGH);
        delay(2000);
        digitalWrite(ENA_PIN, LOW);

        // Reset status menunggu RFID setelah pintu ditutup
        waitingForRFID = false;
        waitStartTime = 0;
        autoClosePending = false; // Nonaktifkan penutupan otomatis
        autoCloseFinished = true; // Tandai proses penutupan otomatis selesai
    }

    // Panggil fungsi untuk memeriksa apakah motor aktif dan pintu dalam proses menutup
    if (motorActive && direction == LOW) {
        antiKecepit();  // Panggil fungsi anti-kejepit
    }
    
    // Kendali LED dan buzzer
    blinkLEDAndBuzzer();

    // Kontrol motor
// Kontrol motor
if (motorActive) {
    if (micros() - lastStepTime >= stepDelay) {
        lastStepTime = micros();

        // Kondisi saat pintu terbuka (CW)
        if (direction == HIGH) {
            if (digitalRead(MAGNET_SENSOR_OPEN_PIN) == LOW) {
                // Sensor terdeteksi bahwa pintu sudah terbuka sepenuhnya
                delay(150);  // Tambahkan delay untuk memastikan motor berhenti dengan stabil
                motorActive = false;
                digitalWrite(ENA_PIN, HIGH);  // Nonaktifkan motor
                doorOpen = true;
                Serial.println("Proses Pintu Terbuka Selesai");
                updateRelayStatus();
                sendDoorOpenNotification();  // Kirim notifikasi saat pintu terbuka
            } else {
                // Pintu masih terbuka, teruskan motor
                digitalWrite(PULL_PIN, HIGH);
                delayMicroseconds(10);
                digitalWrite(PULL_PIN, LOW);
            }
        }
        // Kondisi saat pintu tertutup (CCW)
        else if (direction == LOW) {
            if (digitalRead(MAGNET_SENSOR_CLOSE_PIN) == LOW) {
                // Sensor terdeteksi bahwa pintu sudah tertutup sepenuhnya
                delay(190);  // Tambahkan delay untuk memastikan motor berhenti dengan stabil
                motorActive = false;
                digitalWrite(ENA_PIN, HIGH);  // Nonaktifkan motor
                doorOpen = false;
                Serial.println("Proses Pintu Tertutup Selesai");
                updateRelayStatus();
                sendDoorCloseNotification(); // Kirim notifikasi saat pintu tertutup
            } else {
                // Pintu masih tertutup, teruskan motor
                digitalWrite(PULL_PIN, HIGH);
                delayMicroseconds(10);
                digitalWrite(PULL_PIN, LOW);
            }
        }
    }
}
}
