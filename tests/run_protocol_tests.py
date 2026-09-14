"""Cross-check the production C++ core with Python's independent SHA/HMAC.
All credentials below are public, synthetic test fixtures, never device credentials.
Run on Windows with MinGW g++ on PATH.
"""
import hashlib
import hmac
import pathlib
import shutil
import subprocess
import tempfile
import base64
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
def tag(key, data):
    return hmac.new(key, data.encode() if isinstance(data, str) else data, hashlib.sha256).digest()

def run():
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required for host protocol tests")
    with tempfile.TemporaryDirectory(prefix="kb-ble-pb2-") as directory:
        exe = pathlib.Path(directory) / "pb2_host.exe"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/core_host.cpp"), "-lbcrypt", "-o", str(exe)], check=True)
        process = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        checks = 0
        def ask(line):
            process.stdin.write(line + "\n")
            process.stdin.flush()
            return process.stdout.readline().rstrip("\n")
        def expect(line, expected):
            nonlocal checks
            actual = ask(line)
            assert actual == expected, "protocol assertion failed (values suppressed)"
            checks += 1
        # Synthetic fixtures only.
        secret = hashlib.sha256(b"PB2|PAIR|ABCDEFGH").digest()
        expect("CONFIG bad:id ABCDEFGH ESP32", "NO")
        expect("CONFIG fixture iLoU1234 ESP32", "YES")
        for bad_id in ("bad-id", "bad_id", "bad!", "a"*33):
            expect("CONFIG " + bad_id + " ABCDEFGH ESP32", "NO")
        for bad_code in ("abcd-efgh", "abcdefgh_", "abcdefgh!", "ABCDEFG", "a"*17):
            expect("CONFIG fixture " + bad_code + " ESP32", "NO")
        expect("CONFIG " + "a"*32 + " " + "a"*16 + " ESP32", "YES")
        expect("CONFIG fixture abcdefgh ESP32", "YES")
        expect("READY", "NO")
        expect("PB2:AUTH:" + "0"*64, "PB2:ERROR:AUTH_FAILED")
        def handshake():
            nonce = "01"*16
            caps = ask("PB2:HELLO:" + nonce).split(":")
            assert caps[:6] == ["PB2","CAPS","2","fixture","ESP32","2.0.0"]
            assert caps[7] == tag(secret, "PB2|CAPS|fixture|ESP32|2.0.0|" + nonce + "|" + caps[6]).hex()
            auth = tag(secret, "PB2|AUTH|fixture|" + nonce + "|" + caps[6]).hex()
            ack = ask("PB2:AUTH:" + auth).split(":")
            assert ack[:2] == ["PB2","OK"]
            context = "fixture|" + nonce + "|" + caps[6] + "|" + ack[2]
            assert ack[3] == tag(secret, "PB2|OK|" + context).hex()
            expect("READY", "YES")
            return tag(secret, "PB2|SESSION|" + context)
        def packet(key, seq, kind=1, payload=bytes([0,100,156,0,1,4])):
            body = bytes([170,85,2,kind])+seq.to_bytes(4,"big")+bytes([len(payload)])+payload
            return body + tag(key, body)[:8]
        key = handshake()
        first = packet(key,1)
        expect("PACKET "+first.hex(), "1")
        expect("PACKET "+first.hex(), "0")  # replay
        expect("PACKET "+packet(key,0).hex(), "0")
        corrupted = bytearray(packet(key,2)); corrupted[-1] ^= 1
        expect("PACKET "+corrupted.hex(), "0")
        expect("PACKET "+packet(key,2).hex(), "1")  # bad MAC must not advance sequence
        expect("PACKET "+packet(key,3,payload=bytes([127,0,0,0,0,0])).hex(), "0")
        expect("PACKET "+packet(key,3,kind=2,payload=b"").hex(), "2")
        expect("PACKET "+first[:10].hex(), "0")
        for legacy in ("0","1","Low","J:1,1;1,1","HELLO_APP"):
            expect(legacy,"")
        expect("RESET","RESET")
        expect("READY","NO")
        expect("PACKET "+packet(key,4).hex(),"0")
        new_key = handshake()
        assert new_key != key
        expect("PACKET "+packet(key,4).hex(),"0")
        expect("PACKET "+packet(new_key,1).hex(),"1")
        # Repeated HELLO on the same link is required after entering a pairing code.
        newest_key = handshake()
        expect("PACKET "+packet(new_key,2).hex(),"0")
        expect("PACKET "+packet(newest_key,1).hex(),"1")
        expect("PB2:HELLO:invalid","PB2:ERROR:PROTOCOL_ERROR")
        expect("READY","NO")
        expect("CONFIG fixture ABCDEFGH ESP32-C3","YES")
        caps = ask("PB2:HELLO:"+"0"*32).split(":")
        assert caps[4] == "ESP32-C3"
        expect("PB2:AUTH:"+"0"*64,"PB2:ERROR:AUTH_FAILED")
        expect("READY","NO")
        expect("CONFIG fixture ABCDEFGH ESP32","YES")
        stream_key = handshake()
        fragmented = packet(stream_key,1)
        expect("STREAM 100 "+fragmented[:7].hex(),"")
        expect("STREAM 110 "+fragmented[7:19].hex(),"")
        expect("STREAM 120 "+fragmented[19:].hex(),"1")
        expect("STREAM 130 "+(packet(stream_key,2)+packet(stream_key,3,kind=2,payload=b"")).hex(),"12")
        expired = packet(stream_key,4)
        expect("STREAM 200 "+expired[:10].hex(),"")
        ask("STREAM 600 "+expired[10:].hex())
        expect("STREAM 610 "+packet(stream_key,4).hex(),"1")
        # Oversized text cannot be interpreted as a suffix command.
        expect("STREAM 620 "+(b"x"*120+b"\n").hex(),"")
        expect("STREAM 630 "+b"PB2:HELLO:".hex(),"")
        reply = ask("STREAM 640 "+("01"*16+"\n").encode().hex())
        assert reply.startswith("PB2:CAPS:2:")
        expect("READY","NO")
        expect("STREAM 650 "+packet(stream_key,5).hex(),"0")
        expect("RESET","RESET")
        expect("STREAM 660 "+b"PB2:HEL".hex(),"")
        expect("RESET","RESET")
        expect("STREAM 670 "+("LO:"+"01"*16+"\n").encode().hex(),"")
        expect("READY","NO")
        process.stdin.close()
        assert process.wait(timeout=5) == 0
        print("PASS: PB2 C++ core / independent SHA-HMAC reference; %d assertions plus handshake checks" % checks)

if __name__ == "__main__":
    run()


