# Vueko

## Description

A terminal-integrated web browser

## Prerequisites

### 1. Python Libs
```sh
pip install playwright requests filetype
playwright install
```
Or on certain systems
```sh
python3 -m pip install playwright requests filetype
python3 -m playwright install
```

### 2. System packets
Arch systems
```sh
pacman -S chafa ffmpeg
```

## Architecture
```rust
| --- GET.py // GET webpage
| --- convert.c // takes a mp4 and creates an mp3 & a gif from that
| --- play.c // plays gif & mp3 simulteanously with chafa
| --- help.c // provides help
| --- browser.c // browsing with terminal
```
