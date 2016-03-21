#pragma once

namespace slang {

struct SourceText;

enum class LexerMode {
	Normal,
	Directive,
	IncludeFileName
};

class Lexer {
public:
    Lexer(FileID file, SourceText source, BumpAllocator& alloc, Diagnostics& diagnostics);

    Lexer(const Lexer&) = delete;
    Lexer& operator=(const Lexer&) = delete;

    // lex the next token from the source code
    // will never return a null pointer; at the end of the buffer,
    // an infinite stream of EndOfFile tokens will be generated
    Token* lex(LexerMode mode = LexerMode::Normal);

    FileID getFile() const { return file; }
	BumpAllocator& getAllocator() { return alloc; }
	Diagnostics& getDiagnostics() { return diagnostics; }

private:
    struct TokenInfo {
        StringRef niceText;
        NumericValue numericValue;
        IdentifierType identifierType;
        SyntaxKind directiveKind;
    };

    TokenKind lexToken(TokenInfo& info, bool directiveMode);
    TokenKind lexNumericLiteral(TokenInfo& info);
    TokenKind lexEscapeSequence(TokenInfo& info);
    TokenKind lexDollarSign(TokenInfo& info);
    TokenKind lexDirective(TokenInfo& info);

	Token* lexIncludeFileName();

    void lexStringLiteral(TokenInfo& info);
    void lexRealLiteral(TokenInfo& info, uint64_t value, int decPoint, int digits, bool exponent);
    void lexVectorLiteral(TokenInfo& info, uint64_t size);
    TokenKind lexUnsizedNumericLiteral(TokenInfo& info);
    uint8_t lexTimeUnit();

    template<bool (*IsDigitFunc)(char), uint32_t (*ValueFunc)(char)>
    void lexVectorDigits(TokenInfo& info);

    bool lexTrivia(Buffer<Trivia>& buffer, bool directiveMode);
    char scanUnsignedNumber(char c, uint64_t& unsignedVal, int& digits);

    bool scanBlockComment(Buffer<Trivia>& buffer, bool directiveMode);
    void scanWhitespace(Buffer<Trivia>& buffer);
    void scanLineComment(Buffer<Trivia>& buffer);
    void scanIdentifier();

    int findNextNonWhitespace();

    Token* createToken(TokenKind kind, TokenInfo& info, Buffer<Trivia>& triviaBuffer);
    void addTrivia(TriviaKind kind, Buffer<Trivia>& buffer);
    void addError(DiagCode code);

    // source pointer manipulation
    void mark() { marker = sourceBuffer; }
    void advance() { sourceBuffer++; }
    void advance(int count) { sourceBuffer += count; }
    char peek() { return *sourceBuffer; }
    char peek(int offset) { return sourceBuffer[offset]; }

    // in order to detect embedded nulls gracefully, we call this whenever we
    // encounter a null to check whether we really are at the end of the buffer
    bool reallyAtEnd() { return sourceBuffer >= sourceEnd - 1; }

    uint32_t lexemeLength() { return (uint32_t)(sourceBuffer - marker); }
    StringRef lexeme() { return StringRef(marker, lexemeLength()); }

    bool consume(char c) {
        if (peek() == c) {
            advance();
            return true;
        }
        return false;
    }

    Buffer<char> stringBuffer;
    BufferPool<Trivia> triviaPool;
    VectorBuilder vectorBuilder;
    BumpAllocator& alloc;
	Diagnostics& diagnostics;
    const char* sourceBuffer;
    const char* sourceEnd;
    const char* marker;
    FileID file;
};

// lightweight wrapper around text data that serves as input to the lexer
// this exists to ensure that the input is null-terminated

struct SourceText {
    SourceText(const char* begin, const char* end) :
        ptr(begin), len((uint32_t)(end - begin)) {
        checkErrors();
    }

    SourceText(const Buffer<char>& source) :
        ptr(source.begin()), len(source.count()) {
        checkErrors();
    }

    template<size_t N>
    SourceText(const char(&str)[N]) :
        ptr(str), len(N) {
        static_assert(N > 0, "String literal array must have at least one element");
        checkErrors();
    }

    SourceText(const std::string& source) :
        ptr(source.c_str()), len((uint32_t)source.length() + 1) {
        checkErrors();
    }

    // if you use this, you're guaranteeing that the StringRef points to data that is null terminated
    static SourceText fromNullTerminated(StringRef str) {
        return SourceText(str.begin(), str.end() + 1);
    }

    const char* begin() const { return ptr; }
    const char* end() const { return ptr + len; }
    uint32_t length() const { return len; }

private:
    const char* ptr;
    uint32_t len;

    void checkErrors() {
        ASSERT(ptr);
        ASSERT(len);
        ASSERT(ptr[len - 1] == '\0');
    }
};

}