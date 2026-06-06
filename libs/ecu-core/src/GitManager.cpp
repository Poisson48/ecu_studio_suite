#include "ecu/GitManager.hpp"
#include <git2.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <regex>
#include <sstream>

namespace ecu {

namespace {

// libgit2 keeps a process-wide init refcount; we mirror that here so the
// library is set up exactly once per process regardless of how many
// GitManager instances are alive.
struct Libgit2Guard {
    Libgit2Guard()  { git_libgit2_init(); }
    ~Libgit2Guard() { git_libgit2_shutdown(); }
};

Libgit2Guard& git2Guard() {
    static Libgit2Guard g;
    return g;
}

std::string lastGitError() {
    const git_error* e = git_error_last();
    return e ? e->message : "unknown libgit2 error";
}

// Format a git_time as ISO-8601 with UTC offset, matching %aI from git-log.
std::string formatTime(const git_time& t) {
    const int off   = t.offset;          // minutes east of UTC
    const int hours = std::abs(off) / 60;
    const int mins  = std::abs(off) % 60;
    const char sign = off >= 0 ? '+' : '-';

    std::time_t epoch = static_cast<std::time_t>(t.time) + off * 60;
    std::tm     tm{};
#ifdef _WIN32
    gmtime_s(&tm, &epoch);   // MSVC : arguments inversés vs POSIX
#else
    gmtime_r(&epoch, &tm);
#endif

    return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}{}{:02d}:{:02d}",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        sign, hours, mins);
}

// Parse the decorations string produced by git_reference_foreach_name into
// our GitRef vector, mirroring the JS parseRefs function.
std::vector<GitRef> parseRefs(const std::string& s) {
    if (s.empty()) return {};

    std::vector<GitRef> out;
    std::istringstream  ss(s);
    std::string         tok;

    while (std::getline(ss, tok, ',')) {
        // trim leading/trailing spaces
        const auto first = tok.find_first_not_of(' ');
        const auto last  = tok.find_last_not_of(' ');
        if (first == std::string::npos) continue;
        tok = tok.substr(first, last - first + 1);

        GitRef ref;
        if (tok.starts_with("HEAD -> ")) {
            ref.name = tok.substr(8);
            ref.head = true;
            ref.type = GitRef::Type::Branch;
        } else if (tok == "HEAD") {
            ref.name = "HEAD";
            ref.head = true;
            ref.type = GitRef::Type::Head;
        } else if (tok.starts_with("tag: ")) {
            ref.name = tok.substr(5);
            ref.type = GitRef::Type::Tag;
        } else {
            ref.name = tok;
            ref.type = GitRef::Type::Branch;
        }
        out.push_back(std::move(ref));
    }
    return out;
}

// Build the decoration string for a commit OID by walking all references.
// This reproduces the %D placeholder from git-log.
std::string decorationsFor(git_repository* repo, const git_oid* oid) {
    struct WalkCtx {
        git_repository*  repo;
        const git_oid*   target;
        std::vector<std::string> parts;
    };

    WalkCtx ctx{ repo, oid, {} };

    git_reference_foreach_name(repo, [](const char* refname, void* payload) -> int {
        auto& c = *static_cast<WalkCtx*>(payload);
        git_reference* ref = nullptr;
        if (git_reference_lookup(&ref, c.repo, refname) != 0) return 0;

        git_reference* resolved = nullptr;
        const bool peeled_ok = (git_reference_resolve(&resolved, ref) == 0);
        git_reference_free(ref);
        if (!peeled_ok) return 0;

        const git_oid* target = git_reference_target(resolved);
        const bool match = target && (git_oid_cmp(target, c.target) == 0);
        if (match) {
            const char* sn = git_reference_shorthand(resolved);
            // Determine if HEAD points here.
            git_reference* head = nullptr;
            bool isHead = false;
            if (git_repository_head(&head, c.repo) == 0) {
                const git_oid* headTarget = git_reference_target(head);
                isHead = headTarget && (git_oid_cmp(headTarget, c.target) == 0);
                // Also check if HEAD symbolic target matches this ref name.
                if (!isHead) {
                    const char* headRefname = git_reference_name(head);
                    if (headRefname && std::strcmp(headRefname, git_reference_name(resolved)) == 0)
                        isHead = true;
                }
                git_reference_free(head);
            }

            if (isHead && git_reference_type(resolved) == GIT_REFERENCE_DIRECT) {
                const char* fullname = git_reference_name(resolved);
                if (std::strncmp(fullname, "refs/heads/", 11) == 0) {
                    c.parts.push_back(std::string("HEAD -> ") + sn);
                } else {
                    c.parts.emplace_back("HEAD");
                }
            } else {
                if (git_reference_is_tag(resolved)) {
                    c.parts.push_back(std::string("tag: ") + sn);
                } else {
                    c.parts.emplace_back(sn);
                }
            }
        }
        git_reference_free(resolved);
        return 0;
    }, &ctx);

    std::string result;
    for (std::size_t i = 0; i < ctx.parts.size(); ++i) {
        if (i) result += ", ";
        result += ctx.parts[i];
    }
    return result;
}

bool isBranchNameValid(const std::string& name) {
    static const std::regex kValid{ R"(^[a-zA-Z0-9._/\-]+$)" };
    return std::regex_match(name, kValid);
}

} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────────

GitManager::GitManager(std::string projectDir)
    : m_dir(std::move(projectDir))
{
    git2Guard(); // ensure libgit2 is initialised
}

GitManager::~GitManager() {
    if (m_repo) {
        git_repository_free(m_repo);
        m_repo = nullptr;
    }
}

// Open the repo lazily; succeeds even if the repo does not exist yet (m_repo
// stays null in that case, callers that need it must open before calling).
std::expected<void, std::string> GitManager::openOrNull() {
    if (m_repo) return {};
    const int rc = git_repository_open(&m_repo, m_dir.c_str());
    if (rc == GIT_ENOTFOUND) {
        m_repo = nullptr;
        return {};
    }
    if (rc != 0) return std::unexpected(lastGitError());
    return {};
}

// ── init ─────────────────────────────────────────────────────────────────────

std::expected<void, std::string> GitManager::init() {
    if (m_repo) { git_repository_free(m_repo); m_repo = nullptr; }

    if (git_repository_init(&m_repo, m_dir.c_str(), 0) != 0)
        return std::unexpected(lastGitError());

    git_config* cfg = nullptr;
    if (git_repository_config(&cfg, m_repo) != 0)
        return std::unexpected(lastGitError());

    git_config_set_string(cfg, "user.email", "reprog@local");
    git_config_set_string(cfg, "user.name",  "open-car-reprog");
    git_config_free(cfg);
    return {};
}

// ── Internal staging & committing ────────────────────────────────────────────

std::expected<void, std::string> GitManager::stageAll() {
    git_index* idx = nullptr;
    if (git_repository_index(&idx, m_repo) != 0)
        return std::unexpected(lastGitError());

    git_strarray pathspec{};
    // Stage every tracked and untracked file, like `git add .`
    if (git_index_add_all(idx, &pathspec, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr) != 0) {
        git_index_free(idx);
        return std::unexpected(lastGitError());
    }
    if (git_index_write(idx) != 0) {
        git_index_free(idx);
        return std::unexpected(lastGitError());
    }
    git_index_free(idx);
    return {};
}

std::expected<std::string, std::string>
GitManager::commitStaged(const std::string& message) {
    git_index* idx = nullptr;
    git_tree*  tree = nullptr;
    git_oid    treeOid{};
    git_oid    commitOid{};

    if (git_repository_index(&idx, m_repo) != 0)
        return std::unexpected(lastGitError());

    if (git_index_write_tree(&treeOid, idx) != 0) {
        git_index_free(idx);
        return std::unexpected(lastGitError());
    }
    git_index_free(idx);

    if (git_tree_lookup(&tree, m_repo, &treeOid) != 0)
        return std::unexpected(lastGitError());

    // Build author/committer signature from repo config.
    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) != 0) {
        // Fallback if no identity is configured (should not happen after init()).
        git_signature_now(&sig, "open-car-reprog", "reprog@local");
    }

    // Resolve parent commit (HEAD).
    git_commit* parent = nullptr;
    git_oid     headOid{};
    const bool  hasParent = (git_reference_name_to_id(&headOid, m_repo, "HEAD") == 0 &&
                              git_commit_lookup(&parent, m_repo, &headOid) == 0);

    const git_commit* parents[1] = { parent };
    const int rc = git_commit_create(
        &commitOid,
        m_repo,
        "HEAD",
        sig, sig,
        "UTF-8",
        message.c_str(),
        tree,
        hasParent ? 1u : 0u,
        hasParent ? parents : nullptr);

    git_tree_free(tree);
    git_signature_free(sig);
    if (parent) git_commit_free(parent);

    if (rc != 0) return std::unexpected(lastGitError());

    char buf[GIT_OID_HEXSZ + 1];
    git_oid_tostr(buf, sizeof buf, &commitOid);
    return std::string(buf);
}

// ── commit ───────────────────────────────────────────────────────────────────

CommitResult GitManager::commit(const std::string& message) {
    if (auto r = openOrNull(); !r) return { std::nullopt, std::nullopt, true };
    if (!m_repo)                    return { std::nullopt, std::nullopt, true };

    if (!stageAll()) return { std::nullopt, std::nullopt, true };

    auto result = commitStaged(message);
    if (!result) return { std::nullopt, std::nullopt, true };

    return { std::move(result.value()), message, false };
}

// ── rewordCommit ───────────────────────────────────────────────────────────────
// Réécrit le message d'un commit déjà enregistré. Comme git ne permet pas de
// muter un commit, on recrée la cible avec le nouveau message puis on rejoue
// (re-parent) tous ses descendants sur la branche courante. Arbres, auteurs et
// dates sont conservés : seul le hash change, le contenu de rom.bin est intact.
std::expected<std::string, std::string>
GitManager::rewordCommit(const std::string& hash, const std::string& newMessage) {
    if (auto r = openOrNull(); !r) return std::unexpected(r.error());
    if (!m_repo)                    return std::unexpected("dépôt indisponible");

    git_oid targetOid{};
    if (git_oid_fromstr(&targetOid, hash.c_str()) != 0)
        return std::unexpected("hash invalide : " + hash);

    // Branche courante + son tip (HEAD).
    git_reference* branchRef = nullptr;
    if (git_repository_head(&branchRef, m_repo) != 0)
        return std::unexpected(lastGitError());
    const std::string branchRefName = git_reference_name(branchRef);
    const git_oid     headOid       = *git_reference_target(branchRef);
    git_reference_free(branchRef);

    // Chaîne premier-parent de HEAD jusqu'à la cible (incluse), newest→oldest.
    std::vector<git_oid> chain;
    bool found = false;
    for (git_oid cur = headOid;;) {
        chain.push_back(cur);
        if (git_oid_equal(&cur, &targetOid)) { found = true; break; }
        git_commit* c = nullptr;
        if (git_commit_lookup(&c, m_repo, &cur) != 0) break;
        if (git_commit_parentcount(c) == 0) { git_commit_free(c); break; }
        cur = *git_commit_parent_id(c, 0);
        git_commit_free(c);
    }
    if (!found)
        return std::unexpected("la version sélectionnée n'est pas sur la variante courante");

    std::reverse(chain.begin(), chain.end()); // oldest(cible)→newest(HEAD)

    // Recrée chaque commit ; le 1er garde ses parents d'origine (seul le message
    // change), les suivants voient leur 1er parent remplacé par le commit réécrit.
    git_oid newParentOid{};
    git_oid newTargetOid{};
    git_oid newHeadOid{};
    for (std::size_t i = 0; i < chain.size(); ++i) {
        git_commit* orig = nullptr;
        if (git_commit_lookup(&orig, m_repo, &chain[i]) != 0)
            return std::unexpected(lastGitError());

        git_tree* tree = nullptr;
        if (git_commit_tree(&tree, orig) != 0) {
            git_commit_free(orig);
            return std::unexpected(lastGitError());
        }

        std::vector<git_commit*> parents;
        const unsigned pc = git_commit_parentcount(orig);
        if (i == 0) {
            for (unsigned p = 0; p < pc; ++p) {
                git_commit* par = nullptr;
                if (git_commit_parent(&par, orig, p) == 0) parents.push_back(par);
            }
        } else {
            git_commit* newPar = nullptr;
            if (git_commit_lookup(&newPar, m_repo, &newParentOid) == 0)
                parents.push_back(newPar);
            for (unsigned p = 1; p < pc; ++p) {
                git_commit* par = nullptr;
                if (git_commit_parent(&par, orig, p) == 0) parents.push_back(par);
            }
        }

        std::vector<const git_commit*> cparents(parents.begin(), parents.end());
        const char* msg = (i == 0) ? newMessage.c_str() : git_commit_message(orig);

        git_oid newOid{};
        const int rc = git_commit_create(
            &newOid, m_repo, nullptr,
            git_commit_author(orig), git_commit_committer(orig),
            git_commit_message_encoding(orig), msg,
            tree, cparents.size(),
            cparents.empty() ? nullptr : cparents.data());

        for (git_commit* p : parents) git_commit_free(p);
        git_tree_free(tree);
        git_commit_free(orig);

        if (rc != 0) return std::unexpected(lastGitError());

        if (i == 0) newTargetOid = newOid;
        newParentOid = newOid;
        newHeadOid   = newOid;
    }

    // Pointe la branche courante sur le nouveau tip (force). Les arbres étant
    // identiques, l'index et le répertoire de travail restent cohérents.
    git_reference* updated = nullptr;
    if (git_reference_create(&updated, m_repo, branchRefName.c_str(),
                             &newHeadOid, /*force=*/1,
                             "reword: édition du message de version") != 0)
        return std::unexpected(lastGitError());
    git_reference_free(updated);

    char buf[GIT_OID_HEXSZ + 1];
    git_oid_tostr(buf, sizeof buf, &newTargetOid);
    return std::string(buf);
}

// ── historyChain ───────────────────────────────────────────────────────────────
// Pile undo/redo : la lignée premier-parent de HEAD, du plus récent au plus
// ancien. Lecture pure (aucune mutation), recalculée à chaque nouveau commit.
std::vector<std::string> GitManager::historyChain() {
    if (auto r = openOrNull(); !r) return {};
    if (!m_repo)                    return {};

    git_reference* branchRef = nullptr;
    if (git_repository_head(&branchRef, m_repo) != 0) return {}; // HEAD non né
    git_oid cur = *git_reference_target(branchRef);
    git_reference_free(branchRef);

    std::vector<std::string> out;
    for (;;) {
        char buf[GIT_OID_HEXSZ + 1];
        git_oid_tostr(buf, sizeof buf, &cur);
        out.emplace_back(buf);

        git_commit* c = nullptr;
        if (git_commit_lookup(&c, m_repo, &cur) != 0) break;
        const unsigned pc = git_commit_parentcount(c);
        if (pc == 0) { git_commit_free(c); break; }
        cur = *git_commit_parent_id(c, 0);
        git_commit_free(c);
    }
    return out;
}

// ── log ──────────────────────────────────────────────────────────────────────

std::vector<GitCommit> GitManager::log() {
    if (auto r = openOrNull(); !r) return {};
    if (!m_repo)                    return {};

    git_revwalk* walk = nullptr;
    if (git_revwalk_new(&walk, m_repo) != 0) return {};

    git_revwalk_sorting(walk, GIT_SORT_NONE);
    // --all: push every reference
    git_revwalk_push_glob(walk, "refs/*");

    std::vector<GitCommit> out;
    git_oid oid{};

    while (git_revwalk_next(&oid, walk) == 0) {
        git_commit* c = nullptr;
        if (git_commit_lookup(&c, m_repo, &oid) != 0) continue;

        GitCommit entry;

        char hashBuf[GIT_OID_HEXSZ + 1];
        git_oid_tostr(hashBuf, sizeof hashBuf, &oid);
        entry.hash = hashBuf;

        const unsigned int pc = git_commit_parentcount(c);
        for (unsigned int i = 0; i < pc; ++i) {
            char pbuf[GIT_OID_HEXSZ + 1];
            git_oid_tostr(pbuf, sizeof pbuf, git_commit_parent_id(c, i));
            entry.parents.emplace_back(pbuf);
        }

        const git_signature* auth = git_commit_author(c);
        entry.date   = formatTime(auth->when);
        entry.author = auth->name;

        const char* msg = git_commit_summary(c);
        entry.message = msg ? msg : "";

        entry.refs = parseRefs(decorationsFor(m_repo, &oid));

        out.push_back(std::move(entry));
        git_commit_free(c);
    }

    git_revwalk_free(walk);
    return out;
}

// ── showBinary ───────────────────────────────────────────────────────────────

std::expected<std::vector<uint8_t>, std::string>
GitManager::showBinary(const std::string& hash, const std::string& file) {
    git_oid oid{};
    if (git_oid_fromstr(&oid, hash.c_str()) != 0)
        return std::unexpected("invalid hash: " + hash);

    git_commit* commit = nullptr;
    if (git_commit_lookup(&commit, m_repo, &oid) != 0)
        return std::unexpected(lastGitError());

    git_tree* tree = nullptr;
    if (git_commit_tree(&tree, commit) != 0) {
        git_commit_free(commit);
        return std::unexpected(lastGitError());
    }

    git_tree_entry* entry = nullptr;
    if (git_tree_entry_bypath(&entry, tree, file.c_str()) != 0) {
        git_tree_free(tree);
        git_commit_free(commit);
        return std::vector<uint8_t>{}; // file absent in this commit
    }

    git_object* obj = nullptr;
    if (git_tree_entry_to_object(&obj, m_repo, entry) != 0) {
        git_tree_entry_free(entry);
        git_tree_free(tree);
        git_commit_free(commit);
        return std::unexpected(lastGitError());
    }

    const git_blob* blob = reinterpret_cast<const git_blob*>(obj);
    const auto* data     = static_cast<const uint8_t*>(git_blob_rawcontent(blob));
    const auto  size     = static_cast<std::size_t>(git_blob_rawsize(blob));

    std::vector<uint8_t> buf(data, data + size);

    git_object_free(obj);
    git_tree_entry_free(entry);
    git_tree_free(tree);
    git_commit_free(commit);
    return buf;
}

// ── diffBuffers ──────────────────────────────────────────────────────────────

std::vector<BinaryChange>
GitManager::diffBuffers(const std::vector<uint8_t>& a,
                        const std::vector<uint8_t>& b) {
    std::vector<BinaryChange> changes;
    const std::size_t len = std::max(a.size(), b.size());
    std::size_t i = 0;

    while (i < len) {
        const std::optional<uint8_t> byteA = i < a.size() ? std::optional{a[i]} : std::nullopt;
        const std::optional<uint8_t> byteB = i < b.size() ? std::optional{b[i]} : std::nullopt;

        if (byteA != byteB) {
            const std::size_t start = i;
            while (i < len && (i >= a.size() || i >= b.size() || a[i] != b[i])) ++i;

            BinaryChange ch;
            ch.offset = start;
            if (start < a.size())
                ch.oldBytes.assign(a.begin() + static_cast<std::ptrdiff_t>(start),
                                   a.begin() + static_cast<std::ptrdiff_t>(std::min(i, a.size())));
            if (start < b.size())
                ch.newBytes.assign(b.begin() + static_cast<std::ptrdiff_t>(start),
                                   b.begin() + static_cast<std::ptrdiff_t>(std::min(i, b.size())));
            changes.push_back(std::move(ch));
        } else {
            ++i;
        }
    }

    return changes;
}

// ── diff ─────────────────────────────────────────────────────────────────────

DiffResult GitManager::diff(const std::string& hash) {
    if (auto r = openOrNull(); !r) return { hash, std::nullopt, {}, false, r.error() };
    if (!m_repo) return { hash, std::nullopt, {}, false, "repository not initialised" };

    const auto commits = log();
    const auto it = std::ranges::find_if(commits,
        [&](const GitCommit& c) { return c.hash == hash; });

    if (it == commits.end())
        return { hash, std::nullopt, {}, false, "commit not found" };

    if (it->parents.empty()) {
        auto cur = showBinary(hash, "rom.bin");
        if (!cur) return { hash, std::nullopt, {}, false, cur.error() };
        // First commit: diff against an empty buffer to show all bytes as added.
        auto changes = diffBuffers({}, *cur);
        return { hash, std::nullopt, std::move(changes), true, std::nullopt };
    }

    const std::string& parentHash = it->parents.front();

    auto curBuf    = showBinary(hash,       "rom.bin");
    auto parentBuf = showBinary(parentHash, "rom.bin");

    if (!curBuf)    return { hash, std::nullopt, {}, false, curBuf.error() };
    if (!parentBuf) return { hash, std::nullopt, {}, false, parentBuf.error() };

    auto changes = diffBuffers(*parentBuf, *curBuf);
    return { hash, parentHash, std::move(changes), false, std::nullopt };
}

// ── restore ──────────────────────────────────────────────────────────────────

std::expected<void, std::string> GitManager::restore(const std::string& hash) {
    if (auto r = openOrNull(); !r) return std::unexpected(r.error());
    if (!m_repo) return std::unexpected("repository not initialised");

    // Checkout rom.bin from the given commit into the working directory.
    auto buf = showBinary(hash, "rom.bin");
    if (!buf) return std::unexpected(buf.error());

    // Write the blob to the working-tree file.
    const std::string romPath = m_dir + "/rom.bin";
    FILE* f = std::fopen(romPath.c_str(), "wb");
    if (!f) return std::unexpected("cannot open rom.bin for writing");
    std::fwrite(buf->data(), 1, buf->size(), f);
    std::fclose(f);

    if (auto r = stageAll(); !r)   return std::unexpected(r.error());

    const std::string msg = "Restored to " + hash.substr(0, 8);
    auto result = commitStaged(msg);
    if (!result) return std::unexpected(result.error());
    return {};
}

// ── listBranches (internal) ───────────────────────────────────────────────────

BranchList GitManager::listBranchesRaw() {
    BranchList out;

    git_branch_iterator* it = nullptr;
    if (git_branch_iterator_new(&it, m_repo, GIT_BRANCH_LOCAL) != 0)
        return out;

    git_reference* ref  = nullptr;
    git_branch_t   type = GIT_BRANCH_LOCAL;

    while (git_branch_next(&ref, &type, it) == 0) {
        const char* name = nullptr;
        git_branch_name(&name, ref);
        if (name) {
            out.all.emplace_back(name);
            if (git_branch_is_head(ref))
                out.current = name;
        }
        git_reference_free(ref);
    }
    git_branch_iterator_free(it);
    return out;
}

// ── listBranches ─────────────────────────────────────────────────────────────

std::expected<BranchList, std::string> GitManager::listBranches() {
    if (auto r = openOrNull(); !r) return std::unexpected(r.error());
    if (!m_repo) return BranchList{};
    return listBranchesRaw();
}

// ── createBranch ─────────────────────────────────────────────────────────────

std::expected<std::string, std::string>
GitManager::createBranch(const std::string& name) {
    if (!isBranchNameValid(name))
        return std::unexpected(
            "Nom de branche invalide (lettres, chiffres, . _ / - uniquement)");

    if (auto r = openOrNull(); !r) return std::unexpected(r.error());
    if (!m_repo) return std::unexpected("repository not initialised");

    const auto bl = listBranchesRaw();
    if (std::ranges::find(bl.all, name) != bl.all.end())
        return std::unexpected("La branche \"" + name + "\" existe déjà");

    // Need HEAD commit to create branch from.
    git_oid     headOid{};
    git_commit* headCommit = nullptr;
    if (git_reference_name_to_id(&headOid, m_repo, "HEAD") != 0)
        return std::unexpected(lastGitError());
    if (git_commit_lookup(&headCommit, m_repo, &headOid) != 0)
        return std::unexpected(lastGitError());

    git_reference* newRef = nullptr;
    const int rc = git_branch_create(&newRef, m_repo, name.c_str(), headCommit, 0);
    git_commit_free(headCommit);
    if (rc != 0) return std::unexpected(lastGitError());

    // Checkout (switch) to the new branch.
    git_repository_set_head(m_repo, ("refs/heads/" + name).c_str());
    git_reference_free(newRef);
    return name;
}

// ── switchBranch ─────────────────────────────────────────────────────────────

std::expected<SwitchResult, std::string>
GitManager::switchBranch(const std::string& name) {
    if (auto r = openOrNull(); !r) return std::unexpected(r.error());
    if (!m_repo) return std::unexpected("repository not initialised");

    SwitchResult result{ name, false };

    // Check for dirty working tree (modified / untracked files).
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
               | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    struct DirtyCtx { bool dirty = false; };
    DirtyCtx dirtyCtx;

    git_status_foreach_ext(m_repo, &opts, [](const char*, unsigned int flags, void* payload) -> int {
        auto& ctx = *static_cast<DirtyCtx*>(payload);
        constexpr unsigned int kDirtyMask =
            GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_NEW |
            GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_NEW;
        if (flags & kDirtyMask) ctx.dirty = true;
        return 0;
    }, &dirtyCtx);

    if (dirtyCtx.dirty) {
        const auto bl = listBranchesRaw();
        if (auto r = stageAll(); !r) return std::unexpected(r.error());
        const std::string wipMsg = "WIP on " + bl.current;
        if (auto r = commitStaged(wipMsg); !r) return std::unexpected(r.error());
        result.autoCommitted = true;
    }

    // Switch HEAD to the target branch.
    const std::string refname = "refs/heads/" + name;
    if (git_repository_set_head(m_repo, refname.c_str()) != 0)
        return std::unexpected(lastGitError());

    // Update the working tree to match the new HEAD.
    git_checkout_options chkOpts = GIT_CHECKOUT_OPTIONS_INIT;
    chkOpts.checkout_strategy = GIT_CHECKOUT_SAFE | GIT_CHECKOUT_RECREATE_MISSING;
    if (git_checkout_head(m_repo, &chkOpts) != 0)
        return std::unexpected(lastGitError());

    return result;
}

// ── deleteBranch ─────────────────────────────────────────────────────────────

std::expected<void, std::string>
GitManager::deleteBranch(const std::string& name) {
    if (auto r = openOrNull(); !r) return std::unexpected(r.error());
    if (!m_repo) return std::unexpected("repository not initialised");

    const auto bl = listBranchesRaw();
    if (name == bl.current)
        return std::unexpected("Impossible de supprimer la branche courante");

    git_reference* ref = nullptr;
    if (git_branch_lookup(&ref, m_repo, name.c_str(), GIT_BRANCH_LOCAL) != 0)
        return std::unexpected(lastGitError());

    const int rc = git_branch_delete(ref);
    git_reference_free(ref);
    if (rc != 0) return std::unexpected(lastGitError());
    return {};
}

// ── readFileAtCommit ─────────────────────────────────────────────────────────

std::expected<std::vector<uint8_t>, std::string>
GitManager::readFileAtCommit(const std::string& hash, const std::string& file) {
    if (auto r = openOrNull(); !r) return std::unexpected(r.error());
    if (!m_repo) return std::unexpected("repository not initialised");
    return showBinary(hash, file);
}

} // namespace ecu
