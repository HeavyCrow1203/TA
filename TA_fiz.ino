#include "DHT.h"
#include <FirebaseESP32.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h> 

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define firebase_host "https://tugasakhir-7ca2a-default-rtdb.firebaseio.com/"
#define firebase_auth "Pa6EFXyXDJiAWS1uBJhD3ITvGCmi6VX2HOO2yrVi"

// setup firebase
FirebaseData fbdo;
FirebaseJson json;
String databasePath = "/Data/";
String tempPath = "/Suhu";
String soilPath = "/Kelembaban_Tanah";
String durasiPath = "/Lama_Siram";
String statusPath = "/keterangan";
String timePath = "/Waktu";
String datePath = "/Tanggal";
String parentPath;

float temp, moist, temp_cal, moist_cal;
int adc_soil = 0;
float rendah, normal, tinggi;
float kering, sedang, lembab;
float a1,a2,a3,a4,a5,a6,a7,a8,a9; // a = alpha predikat masing-masing rule
float z1,z2,z3,z4,z5,z6,z7,z8,z9; // z = output masing-masing rule
float r1,r2,r3,r4,r5,r6,r7,r8,r9; // rule = hasil kali alpha (a) dengan output (z)
float sum, alpha, hasil;
String ket, date, waktu;
unsigned long durasi;

// inisialisasi pin pada sensor
#define DHTTYPE DHT22
#define DHTPIN 15
DHT dht(DHTPIN,DHTTYPE);
const int relay = 13;
const int soilPin = 36;

// fuzzyfikasi temp
float suhu_rendah() {
  if(temp <= 20) {
    rendah = 1;
  } else if (temp > 20 && temp < 25) {
    rendah = (25-temp)/(25-20);
  } else if (temp > 25) {
    rendah = 0;
  }
  return rendah;
}

float suhu_normal() {
  if(temp < 20 || temp > 30) {
    normal = 0;
  } else if (temp > 20 && temp < 25) {
    normal = (temp-20)/(25-20);
  } else if (temp > 25 && temp < 30) {
    normal = (30-temp)/(30-25);
  } else if (temp == 25) {
    normal = 1;
  }
  return normal;
}

float suhu_tinggi() {
  if(temp < 25) {
    tinggi = 0;
  } else if (temp > 25 && temp < 30) {
    tinggi = (temp-25)/(30-25);
  } else if (temp >= 30) {
    tinggi = 1;
  }
  return tinggi;
}

// fuzzyfikasi kelembaban tanah
float tanah_kering() {
  if(moist <= 30) {kering=1;}
  else if(moist>30 && moist<50) {kering=(50-moist)/(50-30);}
  else if(moist>50) {kering=0;}
  return kering;
}

float tanah_sedang() {
  if(moist == 50) {sedang=1;}
  else if(moist>30 && moist<50) {sedang=(moist-30)/(50-30);}
  else if(moist>50 && moist<70) {sedang=(70-moist)/(70-50);}
  else if(moist<30 || moist>70) {sedang=0;}
  return sedang;
}

float tanah_lembab() {
  if(moist < 50) {lembab=0;}
  else if(moist>50 && moist<70) {lembab=(moist-50)/(70-50);}
  else if(moist>70) {lembab=1;}
  return lembab;
}

void fuzzyfikasi() {
  suhu_rendah();
  suhu_normal();
  suhu_tinggi();
  tanah_kering();
  tanah_sedang();
  tanah_lembab();
}

void alpha_min() {
  a1 = min(rendah, kering);
  a2 = min(rendah, sedang);
  a3 = min(rendah, lembab);
  a4 = min(normal, kering);
  a5 = min(normal, sedang);
  a6 = min(normal, lembab);
  a7 = min(tinggi, kering);
  a8 = min(tinggi, sedang);
  a9 = min(tinggi, lembab);
}

void rule() {
  z1 = 10+(5*a1);               // if suhu rendah AND moist kering THEN siram banyak
  z2 = (5+(5*a2))+(15-(a2*5));  // if suhu rendah AND moist sedang THEN siram sedang
  z3 = 0;                       // if suhu rendah AND moist lembab THEN tidak siram
  z4 = (5+(5*a4))+(15-(a4*5));  // if suhu sedang AND moist kering THEN siram sedang
  z5 = 10-(5*a5);               // if suhu sedang AND moist sedang THEN siram sedikit
  z6 = 0;                       // if suhu sedang AND moist lembab THEN tidak siram
  z7 = 10+(5*a7);               // if suhu tinggi AND moist kering THEN siram banyak 
  z8 = 10-(5*a8);               // if suhu tinggi AND moist sedang THEN siram sedikit
  z9 = 0;                       // if suhu tinggi AND moist lembab THEN tidak siram
}

void rule_out() {
  r1=a1*z1;
  r2=a2*z2;
  r3=a3*z3;
  r4=a4*z4;
  r5=a5*z5;
  r6=a6*z6;
  r7=a7*z7;
  r8=a8*z8;
  r9=a9*z9;

  sum = r1+r2+r3+r4+r5+r6+r7+r8+r9;
  alpha = a1+a2+a3+a4+a5+a6+a7+a8+a9;
  hasil = sum/alpha;
}

void defuzzyfikasi() {
  fuzzyfikasi();
  alpha_min();
  rule();
  rule_out();
}

String def() {
  defuzzyfikasi();
  if(hasil >= 1 && hasil <= 10) {ket = "Siram Sedikit";}
  else if(hasil > 10 && hasil < 15) {ket = "Siram Sedang";}
  else if(hasil >= 15) {ket = "Siram Banyak";}
  else if(hasil == 0) {ket = "Tidak Siram";}
  return ket;
}

// mendapatkan timestamp dari server ntp
WiFiUDP ntp;
NTPClient waktu32(ntp, "pool.ntp.org");
String bulan[12]={"January", "February", "March", "April", "May", "June", "July", 
                  "August", "September", "October", "November", "December"};
String hari_tanggal;

void getWaktu() {
  waktu32.update();
  time_t epochTime = waktu32.getEpochTime();
  int jam = waktu32.getHours();
  int menit = waktu32.getMinutes();
  int detik = waktu32.getSeconds();

  //mendapatkan tanggal dari epoch time
  struct tm *ptm = gmtime((time_t *)&epochTime);

  int tanggal = ptm->tm_mday;
  int bulan1 = ptm->tm_mon+1;
  String bulansetahun = bulan[bulan1 - 1];
  int tahun = ptm->tm_year+1900;
  waktu = waktu32.getFormattedTime();
  date = String(tanggal)+ " " + String(bulansetahun) +" "+ String(tahun); 
}

void proses_data() {
  temp = dht.readTemperature();
  adc_soil = analogRead(soilPin);
  temp_cal = -8.1903+(temp*1.314);
  moist = map(adc_soil, 1023, 320, 0, 100);
  moist_cal = 101,18-(adc_soil*0.0987);
  defuzzyfikasi();
  def();

  durasi = hasil*1000;
  
  Serial.println(hasil);
  Serial.println(ket);
  getWaktu();
  // mengirim data ke firebase  
  parentPath = databasePath;
  json.set(tempPath.c_str(), String(temp_cal));
  json.set(soilPath.c_str(), String(moist));
  json.set(durasiPath.c_str(), String(hasil));
  json.set(statusPath.c_str(), ket);
  json.set(timePath.c_str(), waktu);
  json.set(datePath.c_str(), date);
  
  Serial.printf("Berhasil Mengirim data ke Firebase... %s\n", Firebase.RTDB.pushJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
  Serial.println("");

  String temp1 = "Temp= "+String(temp_cal)+" *C";
  String moist1 = "Soil= "+String(moist)+" %";

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(temp1);
  lcd.setCursor(0,1);
  lcd.print(moist1);
}

void setup() {
  Serial.begin(115200);
  lcd.begin();
  lcd.setCursor(0,0);
  lcd.print("Eco Garden");
  lcd.setCursor(0,1);
  lcd.print("Starting...");
  lcd.backlight();
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.resetSettings();
  bool res;
  res = wm.autoConnect("LangsungKonek");
  if(!res) {
    Serial.println("Koneksi Gagal");
    ESP.restart();
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Koneksi Gagal");
    lcd.setCursor(0,1);
    lcd.print("Coba Lagi...");
  } else {
    Serial.println("Koneksi Berhasil");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Koneksi Sukses");
  }
  // put your setup code here, to run once:
  Firebase.begin(firebase_host, firebase_auth);
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4069);
  waktu32.begin();
  waktu32.setTimeOffset(8*3300);
  pinMode(relay, OUTPUT);
  pinMode(DHTPIN, INPUT);
  pinMode(soilPin, INPUT);
  analogReadResolution(10);
  dht.begin();
  delay(500);
}

void loop() {
  proses_data();
  digitalWrite(relay, LOW); //relay menyala
  delay(durasi);
  digitalWrite(relay, HIGH); //relay mati
  delay(60000);
}
