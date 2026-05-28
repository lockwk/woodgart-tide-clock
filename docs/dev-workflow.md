# Dev Testing Workflow

How to make changes on the dev branch locally and test them on the Pi.

## 1. Make sure you're on dev

```bash
git checkout dev
```

## 2. Make your changes

Edit whatever files you need to on your Mac.

## 3. Commit and push

```bash
git add .
git commit -m "your message"
git push
```

## 4. SSH into the Pi

```bash
ssh pi@tideclock.local
```

## 5. Stop the production service

```bash
sudo systemctl stop tide-clock-13in3
```

## 6. Pull and deploy to dev

```bash
cd /home/pi/tide-clock-dev
./deploy-dev.sh
```

This pulls the latest dev branch, rebuilds the binary, and starts `tide-clock-13in3-dev`.

## 7. Watch the logs

```bash
journalctl -u tide-clock-13in3-dev -f
```

## 8. When done testing, swap back to production

```bash
sudo systemctl stop tide-clock-13in3-dev
sudo systemctl start tide-clock-13in3
```

---

> **Note:** `deploy-dev.sh` pulls from GitHub as its first step, so always commit and push before SSHing in — otherwise you're testing stale code.
