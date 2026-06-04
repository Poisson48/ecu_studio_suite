#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

struct git_repository;

namespace ecu {

struct GitRef {
    std::string name;
    bool        head = false;
    enum class Type { Branch, Tag, Head } type = Type::Branch;
};

struct GitCommit {
    std::string            hash;
    std::vector<std::string> parents;
    std::string            date;
    std::string            author;
    std::string            message;
    std::vector<GitRef>    refs;
};

struct BinaryChange {
    std::size_t          offset;
    std::vector<uint8_t> oldBytes;
    std::vector<uint8_t> newBytes;
};

struct DiffResult {
    std::string                hash;
    std::optional<std::string> parentHash;
    std::vector<BinaryChange>  changes;
    bool                       isFirst = false;
    std::optional<std::string> error;
};

struct CommitResult {
    std::optional<std::string> hash;
    std::optional<std::string> message;
    bool                       nothing = false;
};

struct BranchList {
    std::string              current;
    std::vector<std::string> all;
};

struct SwitchResult {
    std::string name;
    bool        autoCommitted = false;
};

class GitManager {
public:
    explicit GitManager(std::string projectDir);
    ~GitManager();

    GitManager(const GitManager&)            = delete;
    GitManager& operator=(const GitManager&) = delete;
    GitManager(GitManager&&)                 = delete;
    GitManager& operator=(GitManager&&)      = delete;

    std::expected<void, std::string>           init();
    CommitResult                               commit(const std::string& message);
    // Réécrit le message d'un commit existant (et re-chaîne ses descendants sur
    // la branche courante). Les arbres, auteurs et dates sont préservés ; seuls
    // les hash changent. Renvoie le nouveau hash du commit ré-édité.
    std::expected<std::string, std::string>    rewordCommit(const std::string& hash,
                                                            const std::string& newMessage);
    std::vector<GitCommit>                     log();
    // Chaîne premier-parent depuis HEAD jusqu'à la racine (hash récent → ancien).
    // Sert de pile d'annulation/rétablissement (undo/redo basé sur les versions).
    std::vector<std::string>                   historyChain();
    DiffResult                                 diff(const std::string& hash);
    std::expected<void, std::string>           restore(const std::string& hash);
    std::expected<BranchList, std::string>     listBranches();
    std::expected<std::string, std::string>    createBranch(const std::string& name);
    std::expected<SwitchResult, std::string>   switchBranch(const std::string& name);
    std::expected<void, std::string>           deleteBranch(const std::string& name);
    std::expected<std::vector<uint8_t>, std::string> readFileAtCommit(
        const std::string& hash,
        const std::string& file = "rom.bin");

private:
    std::string      m_dir;
    git_repository*  m_repo = nullptr;

    std::expected<void, std::string> openOrNull();

    std::expected<std::vector<uint8_t>, std::string>
        showBinary(const std::string& hash, const std::string& file);

    static std::vector<BinaryChange>
        diffBuffers(const std::vector<uint8_t>& a,
                    const std::vector<uint8_t>& b);

    std::expected<void, std::string> stageAll();
    std::expected<std::string, std::string> commitStaged(const std::string& message);

    BranchList listBranchesRaw();
};

} // namespace ecu
