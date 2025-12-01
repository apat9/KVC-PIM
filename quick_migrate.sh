#!/bin/bash
# Quick script to help migrate OptiPIM to your own repository

set -e

echo "=========================================="
echo "OptiPIM Repository Migration Helper"
echo "=========================================="
echo ""

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo "Error: Not in a git repository!"
    exit 1
fi

# Show current status
echo "Current repository:"
git remote -v | head -2
echo ""

# Check for uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo "⚠️  You have uncommitted changes."
    echo ""
    read -p "Do you want to commit them now? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        git add .
        git commit -m "Add KV cache-aware PIM framework implementation

- Implement KV cache placement policies (Naive, Bank Partitioning, Contention-Aware)
- Add static weight mapping loader for OptiPIM integration
- Add bank conflict tracking and metrics collection
- Add KV cache trace generator for LLM inference
- Add enhanced PIM trace frontend with KV cache support
- Add evaluation scripts and analysis tools
- Update build system to include new components"
        echo "✅ Changes committed!"
    else
        echo "⚠️  Please commit your changes before migrating."
        exit 1
    fi
fi

echo ""
echo "=========================================="
echo "Step 1: Create a new repository"
echo "=========================================="
echo ""
echo "Please create a new repository on GitHub/GitLab/etc."
echo "Then provide the repository URL below."
echo ""
read -p "Enter your new repository URL (e.g., https://github.com/username/repo.git): " REPO_URL

if [ -z "$REPO_URL" ]; then
    echo "Error: Repository URL is required!"
    exit 1
fi

echo ""
echo "=========================================="
echo "Step 2: Add remote and push"
echo "=========================================="
echo ""

# Check if 'myrepo' remote already exists
if git remote | grep -q "^myrepo$"; then
    echo "⚠️  'myrepo' remote already exists. Removing it..."
    git remote remove myrepo
fi

# Add the new remote
echo "Adding your repository as 'myrepo' remote..."
git remote add myrepo "$REPO_URL"

# Get current branch name
CURRENT_BRANCH=$(git branch --show-current)
echo "Current branch: $CURRENT_BRANCH"

# Push to new repository
echo ""
read -p "Push to your new repository now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Pushing to $REPO_URL..."
    git push myrepo "$CURRENT_BRANCH"
    echo "✅ Successfully pushed to your repository!"
else
    echo "You can push later with: git push myrepo $CURRENT_BRANCH"
fi

echo ""
echo "=========================================="
echo "Step 3: Optional - Set as default remote"
echo "=========================================="
echo ""
read -p "Do you want to set your repository as the default 'origin'? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Rename origin to upstream
    if git remote | grep -q "^origin$"; then
        echo "Renaming 'origin' to 'upstream'..."
        git remote rename origin upstream
    fi
    
    # Rename myrepo to origin
    echo "Setting your repository as 'origin'..."
    git remote rename myrepo origin
    
    echo "✅ Your repository is now the default 'origin'"
    echo "   Original OptiPIM is available as 'upstream'"
    echo ""
    echo "To pull updates from original OptiPIM: git pull upstream $CURRENT_BRANCH"
    echo "To push to your repo: git push origin $CURRENT_BRANCH"
else
    echo ""
    echo "Your repository is available as 'myrepo' remote"
    echo "Original OptiPIM is still 'origin'"
    echo ""
    echo "To push to your repo: git push myrepo $CURRENT_BRANCH"
    echo "To pull from original: git pull origin $CURRENT_BRANCH"
fi

echo ""
echo "=========================================="
echo "✅ Migration complete!"
echo "=========================================="
echo ""
echo "Repository URL: $REPO_URL"
echo ""
echo "Next steps:"
echo "1. Share the repository URL with your research partner"
echo "2. They can clone with: git clone $REPO_URL"
echo "3. Update README.md to document your modifications"
echo ""

