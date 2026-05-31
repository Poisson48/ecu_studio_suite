# Serveur MCP d'ECU Studio

ECU Studio embarque un serveur **MCP (Model Context Protocol)** JSON-RPC 2.0 qui
permet à un assistant comme **Claude** (Claude Desktop / Claude Code) de **piloter
le tuning ECU** : lister les calculateurs, lire des cartographies, comparer des
ROM, appliquer un Stage 1 ou une recette open_damos, vérifier/corriger un
checksum, relocaliser un open_damos en A2L… le tout en **headless**, sans
interface graphique.

C'est un différenciateur face à WinOLS : un agent IA peut explorer et modifier un
firmware de façon scriptable et reproductible.

L'architecture est calquée sur le serveur MCP de SocketSpy
(`apps/socketspy/api/src/mcp`) : même protocole (MCP `2024-11-05`), même transport
stdio (une requête JSON par ligne sur stdin/stdout), et un transport TCP optionnel
lié **exclusivement** à la boucle locale `127.0.0.1`.

## Lancement

Le serveur est intégré au binaire principal et s'active avec un flag :

```bash
# Transport stdio (par défaut — c'est ainsi que Claude lance un serveur MCP)
ecu_studio --mcp

# Transport TCP, boucle locale uniquement (debug / outils tiers)
ecu_studio --mcp-tcp 7892
```

En mode `--mcp`, aucune fenêtre n'est ouverte : seul un `QCoreApplication` est
créé (ECU Studio dépend de Qt Core via `libs/ecu-core`).

## Configuration côté client

### Claude Desktop

Ajoutez l'entrée suivante à votre fichier de configuration Claude Desktop
(`~/.config/Claude/claude_desktop_config.json` sous Linux,
`~/Library/Application Support/Claude/claude_desktop_config.json` sous macOS,
`%APPDATA%/Claude/claude_desktop_config.json` sous Windows). Un exemple prêt à
copier est fourni dans
[`apps/ecu-studio/mcp_config_example.json`](../apps/ecu-studio/mcp_config_example.json) :

```json
{
  "mcpServers": {
    "ecu-studio": {
      "command": "/usr/local/bin/ecu_studio",
      "args": ["--mcp"]
    }
  }
}
```

Adaptez le chemin `command` à l'emplacement de votre binaire `ecu_studio`.

### Claude Code

```bash
claude mcp add ecu-studio /usr/local/bin/ecu_studio --mcp
```

## Répertoire de travail

Les outils qui chargent un `open_damos` (`apply_recipe`, `relocate_open_damos`)
lisent `ressources/<ecu>/open_damos.json` relativement au **répertoire de travail
du serveur** (ou au `resource_dir` passé en paramètre). Lancez le serveur depuis
la racine du dépôt, ou fournissez `resource_dir`.

## Sûreté

- Les outils **de lecture/analyse** n'écrivent jamais sur disque.
- Les outils **d'écriture** écrivent **toujours** dans un fichier de sortie
  distinct (`out_path` / `a2l_out_path`) — jamais sur la ROM source, et **jamais
  sur un périphérique connecté**. Le serveur MCP ne flashe aucun ECU.
- Le transport TCP se lie **uniquement** à `127.0.0.1`.

## Outils exposés

Les ROM sont désignées **par chemin** (mode headless).

| Outil | Type | Description | API ecu-core |
|-------|------|-------------|--------------|
| `list_ecus` | lecture | Liste le catalogue ECU (id, famille, carburant, capacités). | `EcuCatalog::listEcus` |
| `get_ecu` | lecture | Détail d'un ECU : maps Stage 1, paramètres pop&bang. | `EcuCatalog::getEcu` |
| `read_map` | lecture | Lit une cartographie (nx/ny, axes X/Y, grille) à une adresse. | `RomPatcher::readMapData` |
| `read_value` | lecture | Lit un scalaire SWORD big-endian à une adresse. | `RomPatcher::readValue` |
| `find_maps` | lecture | Détecte les cartographies candidates par heuristique. | `MapFinder::findMaps` |
| `compare_roms` | lecture | Diff binaire de deux ROM (intervalles modifiés). | `MapDiffer::diffIntervals` |
| `verify_checksum` | lecture | Vérifie le checksum CRC-16/ARC MPPS (EDC 32K/64K). | `ChecksumEngine::verifyMpps` |
| `list_recipes` | lecture | Liste les recettes d'auto-tune open_damos. | `OpenDamosRecipes::listRecipes` |
| `apply_stage1` | écriture → `out_path` | Applique un % aux maps Stage 1 d'un ECU. | `RomPatcher::applyPctToMap` |
| `apply_recipe` | écriture → `out_path` | Applique une recette open_damos relocalisée. | `OpenDamosRecipes::applyRecipe` |
| `correct_checksum` | écriture → `out_path` | Recalcule et corrige le checksum MPPS. | `ChecksumEngine::correctMpps` |
| `relocate_open_damos` | lecture (+ A2L optionnel → `a2l_out_path`) | Relocalise un open_damos par empreinte d'axes, exporte un A2L. | `OpenDamos::relocate`, `OpenDamosA2lExport::exportA2l` |

## Exemple de session (transport stdio)

```text
→ {"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
← {"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","serverInfo":{"name":"ecu-studio","version":"1.0.0"},"capabilities":{"tools":{"listChanged":false}}}}

→ {"jsonrpc":"2.0","id":2,"method":"tools/list"}
← {"jsonrpc":"2.0","id":2,"result":{"tools":[ ... 12 outils ... ]}}

→ {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_recipes","arguments":{}}}
← {"jsonrpc":"2.0","id":3,"result":{"isError":false,"content":[{"type":"text","text":"{ \"recipes\": [ ... ], \"total\": N }"}]}}

→ {"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"apply_stage1","arguments":{"rom_path":"/tmp/stock.bin","ecu_id":"edc16c34","out_path":"/tmp/stage1.bin"}}}
← {"jsonrpc":"2.0","id":4,"result":{"isError":false,"content":[{"type":"text","text":"{ \"outPath\": \"/tmp/stage1.bin\", ... }"}]}}
```

Le contenu de chaque réponse `tools/call` est du texte JSON (re-parsable) décrivant
le résultat de l'outil.
