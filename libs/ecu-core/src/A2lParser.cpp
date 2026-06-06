#include "ecu/A2lParser.hpp"
#include <QFile>
#include <QHash>
#include <QStringConverter>
#include <optional>
#include <cmath>

namespace ecu {

namespace {

// ---------------------------------------------------------------------------
// Token model. Mirrors the JS tokenizer: B=/begin, E=/end, S=string,
// H=hex int, N=number, I=identifier.
// ---------------------------------------------------------------------------
enum class Tok { B, E, S, H, N, I };

struct Token {
    Tok      t{};
    QString  s{};      // string / identifier text (for S, I)
    double   num = 0;  // numeric value (for N, H)
};

// Streaming tokenizer. Reads the file lazily, one chunk of line at a time, so a
// 200 MB A2L never lives in memory in full. Strips /* */ comments (which may span
// lines) and emits tokens on demand. The whole point of the streaming design is
// to keep peace with huge Bosch/PSA flash descriptions.
class Lexer {
public:
    explicit Lexer(QFile& f) : m_file(f) {}

    // Returns next token, or nullopt at EOF.
    std::optional<Token> next() {
        for (;;) {
            if (m_pos >= m_buf.size()) {
                if (!refill()) return std::nullopt;
                continue;
            }

            const QChar c = m_buf[m_pos];

            // Whitespace.
            if (c.isSpace()) { ++m_pos; continue; }

            // Block comment /* ... */ possibly spanning multiple lines.
            if (c == '/' && peekAt(1) == '*') {
                m_pos += 2;
                if (!skipBlockComment()) return std::nullopt;
                continue;
            }
            // Line comment // (tolerated; not in ASAP2 strictly but harmless).
            if (c == '/' && peekAt(1) == '/') {
                m_pos = m_buf.size();
                continue;
            }

            // /begin and /end keywords.
            if (c == '/') {
                if (matchWord("/begin")) return Token{Tok::B};
                if (matchWord("/end"))   return Token{Tok::E};
                // A bare '/' that is not begin/end: treat as part of an identifier
                // token below (rare, but keeps us from looping).
            }

            // Quoted string (may contain escapes and span lines).
            if (c == '"') return lexString();

            // Everything else: a run of non-space, non-quote characters. We then
            // classify it as hex / number / identifier exactly like the JS regex.
            return lexBare();
        }
    }

private:
    QChar peekAt(int off) const {
        const int p = m_pos + off;
        return p < m_buf.size() ? m_buf[p] : QChar('\0');
    }

    // Ensure at least one full logical line is buffered starting at m_pos.
    // We compact consumed prefix to keep the buffer small.
    bool refill() {
        if (m_pos > 0) {
            m_buf = m_buf.sliced(m_pos);
            m_pos = 0;
        }
        if (m_file.atEnd()) return !m_buf.isEmpty();
        // Read a generous chunk of lines.
        QByteArray chunk = m_file.read(1 << 16); // 64 KiB
        if (chunk.isEmpty()) return !m_buf.isEmpty();

        // DAMOS++ (SunOS) exports interleave NUL bytes — supprime-les.
        if (chunk.contains('\0')) {
            QByteArray clean;
            clean.reserve(chunk.size());
            for (char c : chunk)
                if (c != '\0') clean.append(c);
            chunk = std::move(clean);
        }

        // Décodage robuste et SÛR AUX FRONTIÈRES de chunk : on sniffe l'encodage
        // une fois (UTF-8 si valide, sinon Latin-1 — fréquent pour les DAMOS
        // Bosch, et jamais en échec car mono-octet), puis on garde un décodeur
        // PERSISTANT. L'ancien `QString::fromUtf8(chunk)` par chunk corrompait
        // une séquence multi-octets coupée à chaque frontière de 64 Kio et
        // cassait tout fichier Latin-1.
        if (!m_decInit) {
            QStringDecoder probe(QStringConverter::Utf8);
            (void)probe.decode(chunk);
            m_dec = probe.hasError()
                        ? QStringDecoder(QStringConverter::Latin1)
                        : QStringDecoder(QStringConverter::Utf8);
            m_decInit = true;
        }
        m_buf += m_dec.decode(chunk);
        return true;
    }

    // Skip until "*/", refilling as needed.
    bool skipBlockComment() {
        for (;;) {
            while (m_pos < m_buf.size()) {
                if (m_buf[m_pos] == '*' && peekAt(1) == '/') {
                    m_pos += 2;
                    return true;
                }
                ++m_pos;
            }
            if (!refill()) return false;
            if (m_pos >= m_buf.size() && m_file.atEnd()) return false;
        }
    }

    // Match a fixed keyword at m_pos, ensuring it ends at a delimiter; if matched
    // advance past it. Needs the word fully buffered.
    bool matchWord(const QString& w) {
        while (m_buf.size() - m_pos < w.size() && !m_file.atEnd()) {
            if (!refill()) break;
        }
        if (m_buf.size() - m_pos < w.size()) return false;
        if (m_buf.sliced(m_pos, w.size()) != w) return false;
        const int after = m_pos + w.size();
        const QChar nc = after < m_buf.size() ? m_buf[after] : QChar(' ');
        if (!nc.isSpace() && nc != '/' && nc != '"' && nc != '\0') return false;
        m_pos += w.size();
        return true;
    }

    std::optional<Token> lexString() {
        ++m_pos; // opening quote
        QString out;
        for (;;) {
            while (m_pos < m_buf.size()) {
                const QChar c = m_buf[m_pos++];
                if (c == '\\') {
                    if (m_pos >= m_buf.size() && !refill()) return Token{Tok::S, out};
                    if (m_pos < m_buf.size()) out += m_buf[m_pos++];
                } else if (c == '"') {
                    return Token{Tok::S, out};
                } else {
                    out += c;
                }
            }
            if (!refill()) return Token{Tok::S, out}; // unterminated at EOF
        }
    }

    std::optional<Token> lexBare() {
        // Make sure the whole bare token is buffered (until a delimiter).
        for (;;) {
            int end = m_pos;
            bool foundDelim = false;
            while (end < m_buf.size()) {
                const QChar c = m_buf[end];
                if (c.isSpace() || c == '"') { foundDelim = true; break; }
                ++end;
            }
            if (foundDelim || m_file.atEnd()) {
                const QString word = m_buf.sliced(m_pos, end - m_pos);
                m_pos = end;
                return classify(word);
            }
            if (!refill()) {
                const QString word = m_buf.sliced(m_pos);
                m_pos = m_buf.size();
                return classify(word);
            }
        }
    }

    static std::optional<Token> classify(const QString& w) {
        if (w.isEmpty()) return std::nullopt;
        const QChar c0 = w[0];
        const bool sign = (c0 == '-' || c0 == '+');
        const int  d0   = sign && w.size() > 1 ? 1 : 0;

        // Hex: optional sign, 0x / 0X prefix.
        if (w.size() > d0 + 2 && w[d0] == '0' &&
            (w[d0 + 1] == 'x' || w[d0 + 1] == 'X')) {
            bool ok = false;
            const qlonglong v = w.toLongLong(&ok, 16);
            if (ok) return Token{Tok::H, {}, static_cast<double>(v)};
        }

        // Number: starts with a digit (possibly signed).
        const QChar lead = (sign && w.size() > 1) ? w[1] : c0;
        if (lead.isDigit()) {
            bool ok = false;
            const double v = w.toDouble(&ok);
            if (ok) return Token{Tok::N, {}, v};
            return Token{Tok::N, w, 0}; // keep raw, NaN-equivalent
        }

        return Token{Tok::I, w};
    }

    QFile&         m_file;
    QString        m_buf;
    int            m_pos = 0;
    QStringDecoder m_dec;             // décodeur persistant (UTF-8 ou Latin-1)
    bool           m_decInit = false; // encodage sniffé au 1er chunk
};

// ---------------------------------------------------------------------------
// Intermediate (unresolved) records, kept in hashes for cross-reference.
// ---------------------------------------------------------------------------
struct LayoutSlot { int position = 0; QString dataType; QString indexMode; QString addrType; bool present = false; };

struct RecordLayout {
    QString    name;
    LayoutSlot fncValues;
    LayoutSlot axisX;
    LayoutSlot axisY;
    QString    byteOrder = "BIG_ENDIAN";
};

struct Coeffs { double a=0,b=0,c=0,d=0,e=0,f=0; bool present=false; };

struct CompuMethod {
    QString name;
    QString description;
    QString conversionType;
    QString format;
    QString unit;
    Coeffs  coeffs;
};

struct AxisPts {
    QString  name;
    uint32_t address = 0;
    QString  inputQuantity;
    QString  recordLayout;
    int      maxAxisPoints = 0;
};

struct ParseState {
    QList<Characteristic>     characteristics;
    QHash<QString, RecordLayout> recordLayouts;
    QHash<QString, CompuMethod>  compuMethods;
    QHash<QString, AxisPts>      axisPts;
};

// ---------------------------------------------------------------------------
// Token-stream parser. Pulls from the Lexer with a single-token lookahead so the
// peek()/consume() pattern of the JS source maps directly.
// ---------------------------------------------------------------------------
class Parser {
public:
    explicit Parser(Lexer& lex) : m_lex(lex) {}

    void run(ParseState& st) {
        for (;;) {
            auto t = consume();
            if (!t) break;
            if (t->t != Tok::B) continue;
            parseBlock(valStr(), st);
        }
    }

private:
    // ---- token helpers ----
    const std::optional<Token>& peek() {
        if (!m_have) { m_la = m_lex.next(); m_have = true; }
        return m_la;
    }
    std::optional<Token> consume() {
        if (m_have) { m_have = false; return std::move(m_la); }
        return m_lex.next();
    }
    bool eof() { return !peek().has_value(); }
    bool peekIs(Tok k) { return peek() && peek()->t == k; }

    // val(): consume one token, return its identifier/string text.
    QString valStr() {
        auto t = consume();
        if (!t) return {};
        if (t->t == Tok::S || t->t == Tok::I) return t->s;
        if (t->t == Tok::N) return t->s.isEmpty() ? QString::number(t->num) : t->s;
        if (t->t == Tok::H) return QString::number(static_cast<qlonglong>(t->num));
        return {};
    }
    double numVal() {
        auto t = consume();
        if (!t) return 0.0;
        if (t->t == Tok::H || t->t == Tok::N) return t->num;
        bool ok = false; const double v = t->s.toDouble(&ok);
        return ok ? v : 0.0;
    }

    // skipBlock(): we are just past "/begin TAG"; swallow nested blocks until the
    // matching /end (and its tag name). Mirrors the JS depth counter.
    void skipBlock() {
        int depth = 1;
        while (!eof() && depth > 0) {
            auto t = consume();
            if (!t) break;
            if (t->t == Tok::B) ++depth;
            else if (t->t == Tok::E) { --depth; if (depth == 0) consume(); /* tag */ }
        }
    }

    // ---- sub-parsers (faithful ports) ----
    A2lAxis parseAxisDescr() {
        A2lAxis d;
        d.attribute     = valStr();
        d.inputQuantity = valStr();
        d.conversion    = valStr();
        d.maxAxisPoints = static_cast<int>(numVal());
        d.lowerLimit    = numVal();
        d.upperLimit    = numVal();

        while (!eof()) {
            if (peekIs(Tok::E)) { consume(); consume(); break; }
            if (peekIs(Tok::B)) { consume(); valStr(); skipBlock(); continue; }
            const QString kw = valStr();
            if (kw == "LOWER_LIMIT")      d.lowerLimit = numVal();
            else if (kw == "UPPER_LIMIT") d.upperLimit = numVal();
            else if (kw == "AXIS_PTS_REF") d.axisPtsRef = valStr();
            else if (kw == "FIX_AXIS_PAR") {
                d.hasFixAxisPar = true;
                d.fixAxisOffset = numVal();
                d.fixAxisShift  = numVal();
                d.fixAxisCount  = static_cast<int>(numVal());
            } else if (kw == "FIX_AXIS_PAR_LIST") {
                while (!eof() && !peekIs(Tok::E) && !peekIs(Tok::B) && !peekIs(Tok::I))
                    d.fixAxisList.append(numVal());
            }
        }
        return d;
    }

    Characteristic parseCharacteristic() {
        Characteristic c;
        c.name           = valStr();
        c.longIdentifier = valStr();
        c.type           = valStr();
        c.address        = static_cast<uint32_t>(static_cast<int64_t>(numVal()));
        c.recordLayout   = valStr();
        numVal();                    // maxDiff (unused)
        c.conversion     = valStr();
        c.lowerLimit     = numVal();
        c.upperLimit     = numVal();

        while (!eof()) {
            if (peekIs(Tok::E)) { consume(); consume(); break; }
            if (peekIs(Tok::B)) {
                consume();
                const QString sub = valStr();
                if (sub == "AXIS_DESCR") c.axisDefs.append(parseAxisDescr());
                else skipBlock();
                continue;
            }
            const QString kw = valStr();
            if (kw == "BYTE_ORDER")     c.byteOrder = valStr();
            else if (kw == "BIT_MASK")  c.bitMask = valStr();
            else if (kw == "FORMAT")    c.format = valStr();
            else if (kw == "PHYS_UNIT") c.unit = valStr();
            else if (kw == "NUMBER")    c.nx = static_cast<int>(numVal());
        }
        return c;
    }

    RecordLayout parseRecordLayout() {
        RecordLayout rl;
        rl.name = valStr();
        while (!eof()) {
            if (peekIs(Tok::E)) { consume(); consume(); break; }
            if (peekIs(Tok::B)) { consume(); skipBlock(); continue; }
            const QString kw = valStr();
            auto readSlot = [&](LayoutSlot& s) {
                s.position = static_cast<int>(numVal());
                s.dataType = valStr();
                s.indexMode = valStr();
                s.addrType  = valStr();
                s.present = true;
            };
            if (kw == "FNC_VALUES")        readSlot(rl.fncValues);
            else if (kw == "AXIS_PTS_X")   readSlot(rl.axisX);
            else if (kw == "AXIS_PTS_Y")   readSlot(rl.axisY);
            else if (kw == "NO_AXIS_PTS_X"){ numVal(); valStr(); }
            else if (kw == "NO_AXIS_PTS_Y"){ numVal(); valStr(); }
            else if (kw == "BYTE_ORDER")   rl.byteOrder = valStr();
        }
        return rl;
    }

    CompuMethod parseCompuMethod() {
        CompuMethod cm;
        cm.name           = valStr();
        cm.description    = valStr();
        cm.conversionType = valStr();
        cm.format         = valStr();
        cm.unit           = valStr();
        auto readCoeffs = [&]() {
            cm.coeffs = Coeffs{ numVal(), numVal(), numVal(),
                                numVal(), numVal(), numVal(), true };
        };
        while (!eof()) {
            if (peekIs(Tok::E)) { consume(); consume(); break; }
            if (peekIs(Tok::B)) {
                consume();
                const QString sub = valStr();
                if (sub == "COEFFS") readCoeffs(); else skipBlock();
                continue;
            }
            const QString kw = valStr();
            if (kw == "COEFFS")             readCoeffs();
            else if (kw == "COMPU_TAB_REF") valStr();
            else if (kw == "STATUS_STRING_REF") valStr();
        }
        return cm;
    }

    AxisPts parseAxisPts() {
        AxisPts ap;
        ap.name          = valStr();
        valStr();                                   // description
        ap.address       = static_cast<uint32_t>(static_cast<int64_t>(numVal()));
        ap.inputQuantity = valStr();
        ap.recordLayout  = valStr();
        numVal();                                   // maxDiff
        valStr();                                   // conversion
        ap.maxAxisPoints = static_cast<int>(numVal());
        numVal();                                   // lowerLimit
        numVal();                                   // upperLimit
        while (!eof()) {
            if (peekIs(Tok::E)) { consume(); consume(); break; }
            if (peekIs(Tok::B)) { consume(); skipBlock(); continue; }
            valStr(); // BYTE_ORDER etc. — not needed for resolution
        }
        return ap;
    }

    void parseBlock(const QString& blockName, ParseState& st) {
        if (blockName == "CHARACTERISTIC") {
            Characteristic c = parseCharacteristic();
            if (!c.name.isEmpty()) st.characteristics.append(std::move(c));
        } else if (blockName == "RECORD_LAYOUT") {
            RecordLayout rl = parseRecordLayout();
            if (!rl.name.isEmpty()) st.recordLayouts.insert(rl.name, std::move(rl));
        } else if (blockName == "COMPU_METHOD") {
            CompuMethod cm = parseCompuMethod();
            if (!cm.name.isEmpty()) st.compuMethods.insert(cm.name, std::move(cm));
        } else if (blockName == "AXIS_PTS") {
            AxisPts ap = parseAxisPts();
            if (!ap.name.isEmpty()) st.axisPts.insert(ap.name, std::move(ap));
        } else if (blockName == "COMPU_VTAB" || blockName == "COMPU_VTAB_RANGE") {
            skipBlock(); // value tables not needed for characteristic resolution
        } else if (blockName == "PROJECT" || blockName == "MODULE") {
            // Container blocks: recurse into children, skip inline tokens.
            while (!eof()) {
                if (peekIs(Tok::E)) { consume(); consume(); break; }
                if (peekIs(Tok::B)) { consume(); parseBlock(valStr(), st); continue; }
                consume();
            }
        } else if (blockName == "IF_DATA") {
            skipBlock(); // ECU/tool-specific (Bosch DAMOS, PSA) interface data
        } else {
            skipBlock();
        }
    }

    Lexer&                m_lex;
    std::optional<Token>  m_la;
    bool                  m_have = false;
};

// ---------------------------------------------------------------------------
// Cross-reference resolution (faithful port of JS _enrich / _inferDataType).
// ---------------------------------------------------------------------------
QString inferDataType(const QString& layoutName) {
    if (layoutName.isEmpty()) return "SWORD";
    const QString l = layoutName.toLower();
    auto has = [&](const char* s){ return l.contains(QLatin1String(s)); };
    if (has("u8")  || has("wu8"))  return "UBYTE";
    if (has("s8")  || has("ws8"))  return "SBYTE";
    if (has("u16") || has("wu16")) return "UWORD";
    if (has("s16") || has("ws16")) return "SWORD";
    if (has("u32") || has("wu32")) return "ULONG";
    if (has("s32") || has("ws32")) return "SLONG";
    if (has("f32") || has("float32")) return "FLOAT32_IEEE";
    if (has("f64") || has("float64")) return "FLOAT64_IEEE";
    return "SWORD";
}

int sizeOf(const QString& dt) {
    return A2L_DATA_TYPE_SIZE.value(dt, 2);
}

void enrich(ParseState& st) {
    for (Characteristic& c : st.characteristics) {
        const RecordLayout* rl = st.recordLayouts.contains(c.recordLayout)
                                     ? &st.recordLayouts[c.recordLayout] : nullptr;

        if (rl && rl->fncValues.present) {
            c.dataType = rl->fncValues.dataType;
        } else {
            c.dataType = inferDataType(c.recordLayout);
        }
        c.byteSize = sizeOf(c.dataType);

        // Honour an explicit BYTE_ORDER on the characteristic, else the layout's.
        if (c.byteOrder.isEmpty() || c.byteOrder == "BIG_ENDIAN") {
            if (rl && !rl->byteOrder.isEmpty()) c.byteOrder = rl->byteOrder;
        }

        if (st.compuMethods.contains(c.conversion)) {
            const CompuMethod& cm = st.compuMethods[c.conversion];
            c.conversionType = cm.conversionType;
            if (c.unit.isEmpty()) c.unit = cm.unit;
            if (cm.coeffs.present) {
                const Coeffs& k = cm.coeffs;
                // Simple linear RAT_FUNC: phys = (raw*f - c) / b.
                if (k.a == 0.0 && k.d == 0.0 && k.e == 0.0 && k.b != 0.0) {
                    c.factor = static_cast<float>(k.f / k.b);
                    c.offset = static_cast<float>(-k.c / k.b);
                }
            }
        }

        // Resolve axes. Axis data type comes from the record layout's AXIS_PTS_X/Y
        // slot (STD_AXIS) or from the referenced AXIS_PTS entity (COM_AXIS) — never
        // from AXIS_DESCR, which carries no data type.
        for (int axIdx = 0; axIdx < c.axisDefs.size(); ++axIdx) {
            A2lAxis& axis = c.axisDefs[axIdx];
            if (axis.attribute == "COM_AXIS" && !axis.axisPtsRef.isEmpty()
                && st.axisPts.contains(axis.axisPtsRef)) {
                const AxisPts& ap = st.axisPts[axis.axisPtsRef];
                axis.address = ap.address;
                if (axis.maxAxisPoints == 0) axis.maxAxisPoints = ap.maxAxisPoints;
                const RecordLayout* apRl = st.recordLayouts.contains(ap.recordLayout)
                                               ? &st.recordLayouts[ap.recordLayout] : nullptr;
                axis.dataType = (apRl && apRl->axisX.present) ? apRl->axisX.dataType : "SWORD";
            } else {
                const LayoutSlot* slot = nullptr;
                if (rl) slot = (axIdx == 0) ? &rl->axisX : &rl->axisY;
                axis.dataType = (slot && slot->present) ? slot->dataType : "SWORD";
            }
            axis.byteSize = sizeOf(axis.dataType);

            if (st.compuMethods.contains(axis.conversion)) {
                const CompuMethod& acm = st.compuMethods[axis.conversion];
                if (acm.coeffs.present && acm.coeffs.b != 0.0) {
                    axis.factor = static_cast<float>(acm.coeffs.f / acm.coeffs.b);
                    axis.offset = static_cast<float>(-acm.coeffs.c / acm.coeffs.b);
                }
                axis.unit = acm.unit;
            }
        }

        // Surface axis names / counts on the legacy fields so existing callers
        // that only know nx/ny/axisXName keep working.
        if (c.axisDefs.size() >= 1) {
            c.nx = c.axisDefs[0].maxAxisPoints > 0 ? c.axisDefs[0].maxAxisPoints : c.nx;
            c.axisXName = c.axisDefs[0].axisPtsRef.isEmpty()
                              ? c.axisDefs[0].inputQuantity : c.axisDefs[0].axisPtsRef;
        }
        if (c.axisDefs.size() >= 2) {
            c.ny = c.axisDefs[1].maxAxisPoints > 0 ? c.axisDefs[1].maxAxisPoints : c.ny;
            c.axisYName = c.axisDefs[1].axisPtsRef.isEmpty()
                              ? c.axisDefs[1].inputQuantity : c.axisDefs[1].axisPtsRef;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool A2lParser::parse(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    m_chars.clear();

    Lexer lex(file);
    Parser parser(lex);
    ParseState st;
    parser.run(st);

    enrich(st);
    m_chars = std::move(st.characteristics);
    return true;
}

std::optional<Characteristic> A2lParser::findByAddress(uint32_t address) const {
    for (const auto& c : m_chars)
        if (c.address == address) return c;
    return std::nullopt;
}

std::optional<Characteristic> A2lParser::findByName(const QString& name) const {
    for (const auto& c : m_chars)
        if (c.name == name) return c;
    return std::nullopt;
}

} // namespace ecu
