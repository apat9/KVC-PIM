# Guide: Moving OptiPIM to Your Own Repository

This guide will help you move your modified OptiPIM (with KV cache-aware framework) to your own repository for collaboration.

## Step 1: Commit Your Changes

First, commit all your new files and modifications:

```bash
# Add all new files and modifications
git add .

# Commit with a descriptive message
git commit -m "Add KV cache-aware PIM framework implementation

- Implement KV cache placement policies (Naive, Bank Partitioning, Contention-Aware)
- Add static weight mapping loader for OptiPIM integration
- Add bank conflict tracking and metrics collection
- Add KV cache trace generator for LLM inference
- Add enhanced PIM trace frontend with KV cache support
- Add evaluation scripts and analysis tools
- Update build system to include new components"
```

## Step 2: Create a New Repository

### Option A: GitHub

1. Go to https://github.com/new
2. Create a new repository (e.g., `OptiPIM-KVCache` or `OptiPIM-Research`)
3. **Do NOT** initialize with README, .gitignore, or license (you already have these)
4. Copy the repository URL (e.g., `https://github.com/yourusername/OptiPIM-KVCache.git`)

### Option B: GitLab

1. Go to https://gitlab.com/projects/new
2. Create a new project
3. **Do NOT** initialize with README
4. Copy the repository URL

### Option C: Other Git Hosting

Follow your hosting provider's instructions to create a new repository.

## Step 3: Add Your New Repository as Remote

```bash
# Add your new repository as a remote (we'll call it 'myrepo')
git remote add myrepo https://github.com/yourusername/OptiPIM-KVCache.git

# Verify remotes
git remote -v
```

You should see:
- `origin` pointing to the original OptiPIM repository
- `myrepo` pointing to your new repository

## Step 4: Push to Your New Repository

```bash
# Push your main branch to your new repository
git push myrepo main

# If your default branch is 'master' instead of 'main':
# git push myrepo master
```

## Step 5: Set Your Repository as the Default Remote (Optional)

If you want to make your repository the default:

```bash
# Remove the old origin
git remote remove origin

# Rename your repository to origin
git remote rename myrepo origin

# Or keep both and just use 'myrepo' for pushes
```

## Step 6: Keep Original Repository as Upstream (Recommended)

To keep the original OptiPIM repository for future updates:

```bash
# Rename origin to upstream
git remote rename origin upstream

# Add your repository as origin
git remote add origin https://github.com/yourusername/OptiPIM-KVCache.git

# Push to your repository
git push -u origin main
```

This way you can:
- Pull updates from upstream: `git pull upstream main`
- Push to your repo: `git push origin main`

## Step 7: Update README (Recommended)

Create or update a README section explaining your modifications:

```bash
# Edit README.md to add a section about your modifications
```

Add something like:

```markdown
## KV Cache-Aware Framework Extension

This repository extends OptiPIM with a KV cache-aware PIM framework for LLM inference.

### Key Features

- **KV Cache Placement Policies**: Three policies (Naive, Bank Partitioning, Contention-Aware)
- **Static Weight Integration**: Loads OptiPIM mappings for co-optimization
- **Bank Conflict Tracking**: Monitors and reports conflicts between weights and KV cache
- **LLM Inference Support**: Models autoregressive generation with growing KV cache

See [KV_CACHE_IMPLEMENTATION.md](KV_CACHE_IMPLEMENTATION.md) for detailed documentation.

### Research Contribution

This work addresses the resource contention between static model weights and dynamic KV cache in PIM-based LLM inference systems.
```

## Step 8: Share with Your Research Partner

Once pushed, share the repository URL with your partner:

```bash
# They can clone it with:
git clone https://github.com/yourusername/OptiPIM-KVCache.git
cd OptiPIM-KVCache
```

## Alternative: Create a Fresh Repository (Without History)

If you prefer to start fresh without the original OptiPIM history:

```bash
# Create a new orphan branch (no history)
git checkout --orphan new-main

# Add all files
git add .

# Commit
git commit -m "Initial commit: OptiPIM with KV cache-aware framework"

# Remove old remote
git remote remove origin

# Add your new repository
git remote add origin https://github.com/yourusername/OptiPIM-KVCache.git

# Push
git push -u origin new-main

# Optionally rename branch to main
git branch -M main
git push -u origin main
```

## Important Notes

### License

The original OptiPIM uses **GNU GPL v3** license. Your modifications should:
- Keep the GPL v3 license file
- Acknowledge the original OptiPIM repository
- Follow GPL v3 requirements if you distribute your code

### Attribution

Consider adding attribution in your README:

```markdown
## Acknowledgments

This work extends [OptiPIM](https://github.com/ETHZ-DYNAMO/OptiPIM) by ETHZ-DYNAMO.
```

### .gitignore

Make sure your `.gitignore` includes:
- `build/` directories
- `optipim_env/` (Python virtual environment)
- `llvm-project/` (if you don't want to track it)
- Any other build artifacts

## Quick Commands Summary

```bash
# 1. Commit changes
git add .
git commit -m "Add KV cache-aware framework"

# 2. Add your new repository
git remote add myrepo https://github.com/yourusername/OptiPIM-KVCache.git

# 3. Push
git push myrepo main

# 4. (Optional) Set as default
git remote rename origin upstream
git remote rename myrepo origin
git push -u origin main
```

## Troubleshooting

### If push fails due to authentication:
- Use SSH instead: `git@github.com:yourusername/OptiPIM-KVCache.git`
- Or set up GitHub CLI: `gh auth login`

### If you want to exclude certain files:
```bash
# Add to .gitignore
echo "optipim_env/" >> .gitignore
echo "llvm-project/" >> .gitignore

# Remove from tracking (if already committed)
git rm -r --cached optipim_env/
git rm -r --cached llvm-project/
```

### If you need to update from original OptiPIM later:
```bash
# Fetch updates from original
git fetch upstream

# Merge (if needed)
git merge upstream/main
```

