# esp32-DisplayApp-WebServer
GR-LYCHEE用に作られた、DisplayAppのESP32 WebServerです。  
このESP32アプリと[DisplayAppEsp32][DisplayAppEsp32]を組み合わせると、WiFi経由でカメラ画像を見ることができます。

## 基本的な情報
- ESP32はAPモードで動作します。
- ESP32のIPアドレスは192.168.4.1固定です。
- SSIDは GR-LYCHEE-{MACアドレスの下6桁} です。
- パスワードは gadgetrenesas です。
- ESP32とRZ/A1LUはUART@1Mbpsで接続されているため、1フレーム10KB(QVGAサイズ画質60%設定), 10fpsが限界です。
- ESP32側のUART受信バッファは15KBになっています
- 2.4GHz帯の電波が混み合っていると殆ど動きません

## 書き込み方法
[ビルド済みバイナリ](https://github.com/YuuichiAkagawa/esp32-DisplayApp-webserver/releases/download/v0.1/esp32-DisplayApp-webserver_v0_1.zip)をダウンロードし、展開してください。  
書き込み方法の詳細については
[TCPSocketWiFi_Example_for_ESP32](https://github.com/d-kato/TCPSocketWiFi_Example_for_ESP32)や[RenesasRulz-ESP32を使う](
https://japan.renesasrulz.com/gr_user_forum_japanese/f/gr-lychee/4250/esp32)を参照ください。
```
bootloader.bin           @ 0x1000
partitions_singleapp.bin @ 0x8000
phy_init_data.bin        @ 0xF000
displayappwebsv.bin      @ 0x10000
```

## SSID等の変更方法
main.cの以下の部分を編集して再コンパイルすることで変更が可能です。
```
/*
 * WiFi AP settings
 */
#define WIFI_SSID     "GR-LYCHEE"
#define WIFI_PASSWORD "gadgetrenesas"
#define ADD_MACADDR_TO_SSID (1)
```
- ADD_MACADDR_TO_SSIDを無効化すると、SSIDにMACアドレスが付かなくなります
- esp-idf v2.0でのみ動作します。v2.1RCでは動作しませんでした。

---
Copyright &copy; 2017 Yuuichi Akagawa

Licensed under the Apache License 2.0 as described in the file LICENSE.

[DisplayAppEsp32]: https://github.com/YuuichiAkagawa/GR-Boads_Camera_LCD_sample
