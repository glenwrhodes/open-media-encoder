#!/usr/bin/env bash
# Downloads the small speech model and sample the captions test uses (CI; optional locally) into
# third_party/whisper-models. Without them that test is skipped.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${root}/third_party/whisper-models"
mkdir -p "$dest"

sha256() {
    if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
fetch() { # url file expected-sha256
    local path="${dest}/$2"
    if [[ -f "$path" && "$(sha256 "$path")" == "$3" ]]; then
        echo "$2 already present"
        return
    fi
    curl -fsSL --retry 5 --retry-delay 5 --retry-all-errors -o "${path}.part" "$1"
    if [[ "$(sha256 "${path}.part")" != "$3" ]]; then
        echo "SHA-256 mismatch for $1" >&2
        exit 1
    fi
    mv "${path}.part" "$path"
    echo "Downloaded $2"
}

# Whisper tiny (MIT), pinned revision of github.com/ggerganov/whisper.cpp models on Hugging Face.
fetch "https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-tiny-q5_1.bin" \
    ggml-tiny-q5_1.bin 818710568da3ca15689e31a743197b520007872ff9576237bda97bd1b469c3d7
# The public-domain JFK inaugural address sample from the whisper.cpp repository.
fetch "https://raw.githubusercontent.com/ggml-org/whisper.cpp/d1be6fde11ac6e0407606b4e42fe72d34add8037/samples/jfk.wav" \
    jfk.wav 59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e
