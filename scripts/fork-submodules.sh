#!/bin/bash

# Script to fork all GitLab submodules to GitHub
# Usage: ./fork-submodules.sh <github-username> <github-token>

set -e

GITHUB_USER=${1:-a791143125-arch}
GITHUB_TOKEN=${2:-$GITHUB_TOKEN}

if [ -z "$GITHUB_TOKEN" ]; then
    echo "Error: GitHub token not provided"
    echo "Usage: ./fork-submodules.sh <github-username> <github-token>"
    exit 1
fi

# GitHub API headers
HEADERS=(-H "Authorization: token $GITHUB_TOKEN" -H "Accept: application/vnd.github.v3+json")

# Array of GitLab submodules to fork
declare -a SUBMODULES=(
    "bcg729"
    "bcmatroska2"
    "external/mbedtls"
    "external/bv16-floatingpoint"
    "external/speex"
    "external/libvpx"
    "external/opus"
    "external/gsm"
    "external/srtp"
    "external/libxml2"
    "external/zlib"
    "external/openh264"
    "external/libjpeg-turbo"
    "external/xerces-c"
    "external/soci"
    "external/sqlite3"
    "external/codec2"
    "external/decaf"
    "external/zxing-cpp"
    "external/vo-amrwbenc"
    "external/opencore-amr"
    "external/openldap"
    "external/jsoncpp"
    "external/libyuv"
    "external/liboqs"
    "external/dav1d"
    "external/aom"
    "external/rnnoise"
    "external/cpp-httplib"
)

echo "Starting fork process for all submodules..."
echo "GitHub user: $GITHUB_USER"
echo "Number of submodules to fork: ${#SUBMODULES[@]}"
echo ""

for submodule in "${SUBMODULES[@]}"; do
    # Convert path to repo name (replace / with -)
    repo_name=$(echo "$submodule" | tr '/' '-')
    gitlab_url="https://gitlab.linphone.org/BC/public/${submodule}.git"
    
    echo "Processing: $submodule -> $repo_name"
    
    # Check if repo already exists on GitHub
    REPO_CHECK=$(curl -s "${HEADERS[@]}" "https://api.github.com/repos/$GITHUB_USER/$repo_name")
    
    if echo "$REPO_CHECK" | grep -q '"id"'; then
        echo "  ✓ Repository already exists: $GITHUB_USER/$repo_name"
    else
        echo "  → Importing from GitLab: $gitlab_url"
        
        # Use GitHub's import API
        IMPORT_RESPONSE=$(curl -s -X POST "${HEADERS[@]}" \
            "https://api.github.com/user/imports" \
            -d "{
                \"vcs\": \"git\",
                \"vcs_username\": \"\",
                \"vcs_password\": \"\",
                \"repository_name\": \"$repo_name\",
                \"repository_description\": \"Linphone SDK submodule: $submodule\",
                \"private\": false,
                \"repository_url\": \"$gitlab_url\"
            }")
        
        if echo "$IMPORT_RESPONSE" | grep -q '"status"'; then
            echo "  ✓ Import started for $repo_name"
        else
            echo "  ✗ Failed to import $repo_name"
            echo "    Response: $IMPORT_RESPONSE"
        fi
    fi
    
    echo ""
    sleep 1  # Rate limiting
done

echo "Fork process completed!"
echo ""
echo "Next steps:"
echo "1. Check GitHub import status at https://github.com/settings/repositories"
echo "2. Wait for all imports to complete"
echo "3. Update .gitmodules to point to GitHub URLs"
echo "4. Commit and push the changes"
