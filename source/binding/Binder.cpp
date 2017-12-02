//------------------------------------------------------------------------------
// Binder.h
// Centralized code for converting expressions into an AST.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#include "Binder.h"

#include <algorithm>

#include "symbols/RootSymbol.h"

namespace slang {

Binder::Binder(const Scope& scope) :
    scope(scope), compilation(scope.getCompilation())
{
}

const Expression& Binder::bindConstantExpression(const ExpressionSyntax& syntax) {
    return bindAndPropagate(syntax);
}

const Expression& Binder::bindSelfDeterminedExpression(const ExpressionSyntax& syntax) {
    return bindAndPropagate(syntax);
}

const Expression& Binder::bindAssignmentLikeContext(const ExpressionSyntax& syntax, SourceLocation location, const TypeSymbol& assignmentType) {
    Expression& expr = bindAndPropagate(syntax);
    if (expr.bad())
        return expr;

    const TypeSymbol& type = *expr.type;
    if (!assignmentType.isAssignmentCompatible(type)) {
        DiagCode code = assignmentType.isCastCompatible(type) ? DiagCode::NoImplicitConversion : DiagCode::BadAssignment;
        compilation.addError(code, location) << syntax.sourceRange();
        return badExpr(&expr);
    }

    if (!propagateAssignmentLike(expr, assignmentType))
        expr.propagateType(*expr.type);

    // TODO: truncation
    return expr;
}

const Statement& Binder::bindStatement(const StatementSyntax& syntax) const {
    switch (syntax.kind) {
        case SyntaxKind::ReturnStatement:
            return bindReturnStatement((const ReturnStatementSyntax&)syntax);
        case SyntaxKind::ConditionalStatement:
            return bindConditionalStatement((const ConditionalStatementSyntax&)syntax);
        case SyntaxKind::ForLoopStatement:
            return bindForLoopStatement((const ForLoopStatementSyntax&)syntax);
        case SyntaxKind::ExpressionStatement:
            return bindExpressionStatement((const ExpressionStatementSyntax&)syntax);
        default: THROW_UNREACHABLE;
    }
}

const StatementList& Binder::bindStatementList(const SyntaxList<SyntaxNode>& items) const {
    SmallVectorSized<const Statement*, 8> buffer;
    for (auto member : scope.members()) {
        if (member->kind == SymbolKind::Variable)
            buffer.append(compilation.emplace<VariableDeclStatement>(member->as<VariableSymbol>()));
    }

    for (const auto& item : items) {
        if (isStatement(item->kind))
            buffer.append(&bindStatement(item->as<StatementSyntax>()));
    }

    return *compilation.emplace<StatementList>(buffer.copy(compilation));
}

Expression& Binder::bindAndPropagate(const ExpressionSyntax& syntax) {
    Expression& expr = bindExpression(syntax);
    expr.propagateType(*expr.type);
    return expr;
}

Expression& Binder::bindExpression(const ExpressionSyntax& syntax) {
    switch (syntax.kind) {
        case SyntaxKind::NullLiteralExpression:
        case SyntaxKind::StringLiteralExpression:
        case SyntaxKind::TimeLiteralExpression:
        case SyntaxKind::WildcardLiteralExpression:
        case SyntaxKind::OneStepLiteralExpression:
            // TODO: unimplemented
            break;
        case SyntaxKind::IdentifierName:
        case SyntaxKind::IdentifierSelectName:
        case SyntaxKind::ScopedName:
            return bindName(syntax.as<NameSyntax>());
        case SyntaxKind::RealLiteralExpression:
        case SyntaxKind::IntegerLiteralExpression:
        case SyntaxKind::UnbasedUnsizedLiteralExpression:
            return bindLiteral(syntax.as<LiteralExpressionSyntax>());
        case SyntaxKind::IntegerVectorExpression:
            return bindLiteral(syntax.as<IntegerVectorExpressionSyntax>());
        case SyntaxKind::ParenthesizedExpression:
            return bindExpression(syntax.as<ParenthesizedExpressionSyntax>().expression);
        case SyntaxKind::UnaryPlusExpression:
        case SyntaxKind::UnaryMinusExpression:
        case SyntaxKind::UnaryBitwiseNotExpression:
            return bindUnaryArithmeticOperator(syntax.as<PrefixUnaryExpressionSyntax>());
        case SyntaxKind::UnaryBitwiseAndExpression:
        case SyntaxKind::UnaryBitwiseOrExpression:
        case SyntaxKind::UnaryBitwiseXorExpression:
        case SyntaxKind::UnaryBitwiseNandExpression:
        case SyntaxKind::UnaryBitwiseNorExpression:
        case SyntaxKind::UnaryBitwiseXnorExpression:
        case SyntaxKind::UnaryLogicalNotExpression:
            return bindUnaryReductionOperator(syntax.as<PrefixUnaryExpressionSyntax>());
        case SyntaxKind::AddExpression:
        case SyntaxKind::SubtractExpression:
        case SyntaxKind::MultiplyExpression:
        case SyntaxKind::DivideExpression:
        case SyntaxKind::ModExpression:
        case SyntaxKind::BinaryAndExpression:
        case SyntaxKind::BinaryOrExpression:
        case SyntaxKind::BinaryXorExpression:
        case SyntaxKind::BinaryXnorExpression:
            return bindArithmeticOperator(syntax.as<BinaryExpressionSyntax>());
        case SyntaxKind::EqualityExpression:
        case SyntaxKind::InequalityExpression:
        case SyntaxKind::CaseEqualityExpression:
        case SyntaxKind::CaseInequalityExpression:
        case SyntaxKind::GreaterThanEqualExpression:
        case SyntaxKind::GreaterThanExpression:
        case SyntaxKind::LessThanEqualExpression:
        case SyntaxKind::LessThanExpression:
        case SyntaxKind::WildcardEqualityExpression:
        case SyntaxKind::WildcardInequalityExpression:
            return bindComparisonOperator(syntax.as<BinaryExpressionSyntax>());
        case SyntaxKind::LogicalAndExpression:
        case SyntaxKind::LogicalOrExpression:
        case SyntaxKind::LogicalImplicationExpression:
        case SyntaxKind::LogicalEquivalenceExpression:
            return bindRelationalOperator(syntax.as<BinaryExpressionSyntax>());
        case SyntaxKind::LogicalShiftLeftExpression:
        case SyntaxKind::LogicalShiftRightExpression:
        case SyntaxKind::ArithmeticShiftLeftExpression:
        case SyntaxKind::ArithmeticShiftRightExpression:
        case SyntaxKind::PowerExpression:
            return bindShiftOrPowerOperator(syntax.as<BinaryExpressionSyntax>());
        case SyntaxKind::AssignmentExpression:
        case SyntaxKind::AddAssignmentExpression:
        case SyntaxKind::SubtractAssignmentExpression:
        case SyntaxKind::MultiplyAssignmentExpression:
        case SyntaxKind::DivideAssignmentExpression:
        case SyntaxKind::ModAssignmentExpression:
        case SyntaxKind::AndAssignmentExpression:
        case SyntaxKind::OrAssignmentExpression:
        case SyntaxKind::XorAssignmentExpression:
        case SyntaxKind::LogicalLeftShiftAssignmentExpression:
        case SyntaxKind::LogicalRightShiftAssignmentExpression:
        case SyntaxKind::ArithmeticLeftShiftAssignmentExpression:
        case SyntaxKind::ArithmeticRightShiftAssignmentExpression:
            return bindAssignmentOperator(syntax.as<BinaryExpressionSyntax>());
        case SyntaxKind::InvocationExpression:
            return bindSubroutineCall(syntax.as<InvocationExpressionSyntax>());
        case SyntaxKind::ConditionalExpression:
            return bindConditionalExpression(syntax.as<ConditionalExpressionSyntax>());
        case SyntaxKind::ConcatenationExpression:
            return bindConcatenationExpression(syntax.as<ConcatenationExpressionSyntax>());
        case SyntaxKind::MultipleConcatenationExpression:
            return bindMultipleConcatenationExpression(syntax.as<MultipleConcatenationExpressionSyntax>());
        case SyntaxKind::ElementSelectExpression:
            return bindSelectExpression(syntax.as<ElementSelectExpressionSyntax>());
        default: THROW_UNREACHABLE;
    }
    return badExpr(nullptr);
}

Expression& Binder::bindLiteral(const LiteralExpressionSyntax& syntax) {
    switch (syntax.kind) {
        case SyntaxKind::IntegerLiteralExpression:
            return *compilation.emplace<IntegerLiteral>(compilation, compilation.getIntType(),
                                                    syntax.literal.intValue(), syntax);
        case SyntaxKind::RealLiteralExpression:
            return *compilation.emplace<RealLiteral>(compilation.getRealType(), syntax.literal.realValue(), syntax);
        case SyntaxKind::UnbasedUnsizedLiteralExpression: {
            // UnsizedUnbasedLiteralExpressions default to a size of 1 in an undetermined
            // context, but can grow
            logic_t val = syntax.literal.bitValue();
            return *compilation.emplace<UnbasedUnsizedIntegerLiteral>(compilation.getType(1, false, val.isUnknown()), val, syntax);
        }
        default: THROW_UNREACHABLE;
    }
}

Expression& Binder::bindLiteral(const IntegerVectorExpressionSyntax& syntax) {
    if (syntax.value.isMissing())
        return badExpr(compilation.emplace<IntegerLiteral>(compilation, compilation.getErrorType(), SVInt::Zero, syntax));

    const SVInt& value = syntax.value.intValue();
    const TypeSymbol& type = compilation.getType(value.getBitWidth(), value.isSigned(), value.hasUnknown());
    return *compilation.emplace<IntegerLiteral>(compilation, type, value, syntax);
}

Expression& Binder::bindName(const NameSyntax& syntax) {
    switch (syntax.kind) {
        case SyntaxKind::IdentifierName:
            return bindSimpleName(syntax.as<IdentifierNameSyntax>());
        case SyntaxKind::IdentifierSelectName:
            return bindSelectName(syntax.as<IdentifierSelectNameSyntax>());
        case SyntaxKind::ScopedName:
            return bindScopedName(syntax.as<ScopedNameSyntax>());
        default: THROW_UNREACHABLE;
    }
}

Expression& Binder::bindSimpleName(const IdentifierNameSyntax& syntax) {
    string_view identifier = syntax.identifier.valueText();
    LookupResult result;
    scope.lookup(identifier, result);

    if (result.getResultKind() != LookupResult::Found) {
        compilation.addError(DiagCode::UndeclaredIdentifier, syntax.identifier.location()) << identifier;
        return badExpr(nullptr);
    }

    const Symbol* symbol = result.getFoundSymbol();
    switch (symbol->kind) {
        case SymbolKind::Variable:
        case SymbolKind::FormalArgument:
            return *compilation.emplace<VariableRefExpression>(symbol->as<VariableSymbol>(), syntax);

        case SymbolKind::Parameter:
            return *compilation.emplace<ParameterRefExpression>(symbol->as<ParameterSymbol>(), syntax);

        default: THROW_UNREACHABLE;
    }
}

Expression& Binder::bindSelectName(const IdentifierSelectNameSyntax& syntax) {
    // TODO: once we fully support more complex non-integral types and actual support
    // part selects, we need to be able to handle multiple accesses like
    // foo[2 : 4][3 : 1][7 : 8] where each access depends on the type of foo, not just the type of the preceding
    // expression. For now though, we implement the most simple case:
    // foo[SELECT] where foo is an integral type.

    ASSERT(syntax.selectors.count() == 1);
    ASSERT(syntax.selectors[0]->selector);
    // spoof this being just a simple ElementSelectExpression
    return bindSelectExpression(syntax,
        bindName(*compilation.emplace<IdentifierNameSyntax>(syntax.identifier)), *syntax.selectors[0]->selector);
}

Expression& Binder::bindScopedName(const ScopedNameSyntax& syntax) {
    // TODO: only handles packages right now
    if (syntax.separator.kind != TokenKind::DoubleColon || syntax.left.kind != SyntaxKind::IdentifierName)
        return badExpr(nullptr);

    string_view identifier = syntax.left.as<IdentifierNameSyntax>().identifier.valueText();
    if (identifier.empty())
        return badExpr(nullptr);

    auto package = scope.asSymbol().getRoot().findPackage(identifier);
    if (!package)
        return badExpr(nullptr);

    return Binder(*package).bindName(syntax.right);
}

Expression& Binder::bindUnaryArithmeticOperator(const PrefixUnaryExpressionSyntax& syntax) {
    // Supported for both integral and real types. Can be overloaded for others.
    Expression* operand = &bindAndPropagate(syntax.operand);
    if (!checkOperatorApplicability(syntax.kind, syntax.operatorToken.location(), &operand))
        return badExpr(compilation.emplace<UnaryExpression>(compilation.getErrorType(), *operand, syntax));

    return *compilation.emplace<UnaryExpression>(*operand->type, *operand, syntax);
}

Expression& Binder::bindUnaryReductionOperator(const PrefixUnaryExpressionSyntax& syntax) {
    // Result type is always a single bit. Supported on integral types.
    Expression* operand = &bindAndPropagate(syntax.operand);
    if (!checkOperatorApplicability(syntax.kind, syntax.operatorToken.location(), &operand))
        return badExpr(compilation.emplace<UnaryExpression>(compilation.getErrorType(), *operand, syntax));

    return *compilation.emplace<UnaryExpression>(compilation.getLogicType(), *operand, syntax);
}

Expression& Binder::bindArithmeticOperator(const BinaryExpressionSyntax& syntax) {
    Expression* lhs = &bindAndPropagate(syntax.left);
    Expression* rhs = &bindAndPropagate(syntax.right);
    if (!checkOperatorApplicability(syntax.kind, syntax.operatorToken.location(), &lhs, &rhs))
        return badExpr(compilation.emplace<BinaryExpression>(compilation.getErrorType(), *lhs, *rhs, syntax));

    // Get the result type; force the type to be four-state if it's a division, which can make a 4-state output
    // out of 2-state inputs
    const TypeSymbol& type = binaryOperatorResultType(lhs->type, rhs->type, syntax.kind == SyntaxKind::DivideExpression);
    return *compilation.emplace<BinaryExpression>(type, *lhs, *rhs, syntax);
}

Expression& Binder::bindComparisonOperator(const BinaryExpressionSyntax& syntax) {
    Expression* lhs = &bindAndPropagate(syntax.left);
    Expression* rhs = &bindAndPropagate(syntax.right);
    if (!checkOperatorApplicability(syntax.kind, syntax.operatorToken.location(), &lhs, &rhs))
        return badExpr(compilation.emplace<BinaryExpression>(compilation.getErrorType(), *lhs, *rhs, syntax));

    // result type is always a single bit
    return *compilation.emplace<BinaryExpression>(compilation.getLogicType(), *lhs, *rhs, syntax);
}

Expression& Binder::bindRelationalOperator(const BinaryExpressionSyntax& syntax) {
    Expression* lhs = &bindAndPropagate(syntax.left);
    Expression* rhs = &bindAndPropagate(syntax.right);
    if (!checkOperatorApplicability(syntax.kind, syntax.operatorToken.location(), &lhs, &rhs))
        return badExpr(compilation.emplace<BinaryExpression>(compilation.getErrorType(), *lhs, *rhs, syntax));

    // operands are sized to max(l,r) and the result of the operation is always 1 bit
    // no propagations from above have an actual have an effect on the subexpressions
    // This logic is similar to that of assignment operators, except for the
    // reciprocality
    if (!propagateAssignmentLike(*rhs, *lhs->type))
        propagateAssignmentLike(*lhs, *rhs->type);

    // result type is always a single bit
    return *compilation.emplace<BinaryExpression>(compilation.getLogicType(), *lhs, *rhs, syntax);
}

Expression& Binder::bindShiftOrPowerOperator(const BinaryExpressionSyntax& syntax) {
    // The shift and power operators are handled together here because in both cases the second
    // operand is evaluated in a self determined context.
    Expression* lhs = &bindAndPropagate(syntax.left);
    Expression* rhs = &bindAndPropagate(syntax.right);
    if (!checkOperatorApplicability(syntax.kind, syntax.operatorToken.location(), &lhs, &rhs))
        return badExpr(compilation.emplace<BinaryExpression>(compilation.getErrorType(), *lhs, *rhs, syntax));

    // Power operator can result in division by zero 'x
    const TypeSymbol& type = binaryOperatorResultType(lhs->type, rhs->type, syntax.kind == SyntaxKind::PowerExpression);

    return *compilation.emplace<BinaryExpression>(type, *lhs, *rhs, syntax);
}

Expression& Binder::bindAssignmentOperator(const BinaryExpressionSyntax& syntax) {
    Expression* lhs = &bindAndPropagate(syntax.left);
    Expression* rhs = &bindAndPropagate(syntax.right);

    // Basic assignment (=) is always applicable, but operators like += are applicable iff
    // the associated binary operator is applicable
    SyntaxKind binopKind = SyntaxKind::Unknown;
    switch (syntax.kind) {
        case SyntaxKind::AssignmentExpression: binopKind = SyntaxKind::Unknown; break;
        case SyntaxKind::AddAssignmentExpression: binopKind = SyntaxKind::AddExpression; break;
        case SyntaxKind::SubtractAssignmentExpression: binopKind = SyntaxKind::SubtractExpression; break;
        case SyntaxKind::MultiplyAssignmentExpression: binopKind = SyntaxKind::MultiplyExpression; break;
        case SyntaxKind::DivideAssignmentExpression: binopKind = SyntaxKind::DivideExpression; break;
        case SyntaxKind::ModAssignmentExpression: binopKind = SyntaxKind::ModExpression; break;
        case SyntaxKind::AndAssignmentExpression: binopKind = SyntaxKind::BinaryAndExpression; break;
        case SyntaxKind::OrAssignmentExpression: binopKind = SyntaxKind::BinaryOrExpression; break;
        case SyntaxKind::XorAssignmentExpression: binopKind = SyntaxKind::BinaryXorExpression; break;
        case SyntaxKind::LogicalLeftShiftAssignmentExpression: binopKind = SyntaxKind::LogicalShiftLeftExpression; break;
        case SyntaxKind::LogicalRightShiftAssignmentExpression: binopKind = SyntaxKind::LogicalShiftRightExpression; break;
        case SyntaxKind::ArithmeticLeftShiftAssignmentExpression: binopKind = SyntaxKind::ArithmeticShiftLeftExpression; break;
        case SyntaxKind::ArithmeticRightShiftAssignmentExpression: binopKind = SyntaxKind::ArithmeticShiftRightExpression; break;
        default: THROW_UNREACHABLE;
    }
    // TODO: the LHS has to be assignable (i.e not a general expression)
    if (!checkOperatorApplicability(binopKind, syntax.operatorToken.location(), &lhs, &rhs))
        return badExpr(compilation.emplace<BinaryExpression>(compilation.getErrorType(), *lhs, *rhs, syntax));

    // The operands of an assignment are themselves self determined,
    // but we must increase the size of the RHS to the size of the LHS if it is larger, and then
    // propagate that information down
    propagateAssignmentLike(*rhs, *lhs->type);

    // result type is always the type of the left hand side
    return *compilation.emplace<BinaryExpression>(*lhs->type, *lhs, *rhs, syntax);
}

Expression& Binder::bindSubroutineCall(const InvocationExpressionSyntax& syntax) {
    // TODO: check for something other than a simple name on the LHS
    auto name = syntax.left.getFirstToken();
    LookupResult result;
    result.nameKind = LookupNameKind::Callable;
    scope.lookup(name.valueText(), result);
    const Symbol* symbol = result.getFoundSymbol();
    ASSERT(symbol && symbol->kind == SymbolKind::Subroutine);

    auto actualArgs = syntax.arguments->parameters;
    const SubroutineSymbol& subroutine = symbol->as<SubroutineSymbol>();

    // TODO: handle too few args as well, which requires looking at default values
    auto formalArgs = subroutine.arguments;
    if (formalArgs.size() < actualArgs.count()) {
        auto& diag = compilation.addError(DiagCode::TooManyArguments, name.location());
        diag << syntax.left.sourceRange();
        diag << (int)formalArgs.size();
        diag << actualArgs.count();
        return badExpr(nullptr);
    }

    // TODO: handle named arguments in addition to ordered
    SmallVectorSized<const Expression*, 8> buffer;
    for (uint32_t i = 0; i < actualArgs.count(); i++) {
        const auto& arg = actualArgs[i]->as<OrderedArgumentSyntax>();
        buffer.append(&bindAssignmentLikeContext(arg.expr, arg.sourceRange().start(), *formalArgs[i]->type));
    }

    return *compilation.emplace<CallExpression>(subroutine, buffer.copy(compilation), syntax);
}

Expression& Binder::bindConditionalExpression(const ConditionalExpressionSyntax& syntax) {
    // TODO: handle the pattern matching conditional predicate case, rather than just assuming that it's a simple
    // expression
    ASSERT(syntax.predicate.conditions.count() == 1);
    Expression& pred = bindAndPropagate(syntax.predicate.conditions[0]->expr);
    Expression& left = bindAndPropagate(syntax.left);
    Expression& right = bindAndPropagate(syntax.right);

    // TODO: handle non-integral and non-real types properly
    // force four-state return type for ambiguous condition case
    const TypeSymbol& type = binaryOperatorResultType(left.type, right.type, true);
    return *compilation.emplace<TernaryExpression>(type, pred, left, right, syntax);
}

Expression& Binder::bindConcatenationExpression(const ConcatenationExpressionSyntax& syntax) {
    SmallVectorSized<const Expression*, 8> buffer;
    int totalWidth = 0;
    for (auto argSyntax : syntax.expressions) {
        const Expression& arg = bindAndPropagate(*argSyntax);
        buffer.append(&arg);

        const TypeSymbol& type = *arg.type;
        if (type.kind != SymbolKind::IntegralType)
            return badExpr(compilation.emplace<NaryExpression>(compilation.getErrorType(), nullptr, syntax));

        totalWidth += type.width();
    }

    return *compilation.emplace<NaryExpression>(compilation.getType(totalWidth, false), buffer.copy(compilation), syntax);
}

Expression& Binder::bindMultipleConcatenationExpression(const MultipleConcatenationExpressionSyntax& syntax) {
    Expression& left  = bindAndPropagate(syntax.expression);
    Expression& right = bindAndPropagate(syntax.concatenation);
    // TODO: check applicability
    // TODO: left must be compile-time evaluatable, and it must be known in order to
    // compute the type of a multiple concatenation. Have a nice error when this isn't the case?
    // TODO: in cases like these, should we bother storing the bound expression? should we at least cache the result
    // so we don't have to compute it again elsewhere?
    uint16_t replicationTimes = left.eval().integer().as<uint16_t>().value();
    return *compilation.emplace<BinaryExpression>(compilation.getType(right.type->width() * replicationTimes, false), left, right, syntax);
}

Expression& Binder::bindSelectExpression(const ElementSelectExpressionSyntax& syntax) {
    Expression& expr = bindAndPropagate(syntax.left);
    // TODO: null selector?
    return bindSelectExpression(syntax, expr, *syntax.select.selector);
}

Expression& Binder::bindSelectExpression(const ExpressionSyntax& syntax, Expression& expr, const SelectorSyntax& selector) {
    // if (down), the indices are declares going down, [15:0], so
    // msb > lsb
    if (expr.bad())
        return badExpr(&expr);

    bool down = expr.type->as<IntegralTypeSymbol>().lowerBounds[0] >= 0;
    Expression* left = nullptr;
    Expression* right = nullptr;
    int width = 0;

    // TODO: errors if things that should be constant expressions aren't actually constant expressions
    SyntaxKind kind = selector.kind;
    switch (kind) {
        case SyntaxKind::BitSelect:
            left = &bindAndPropagate(selector.as<BitSelectSyntax>().expr);
            right = left;
            width = 1;
            break;
        case SyntaxKind::SimpleRangeSelect:
            left = &bindAndPropagate(selector.as<RangeSelectSyntax>().left); // msb
            right = &bindAndPropagate(selector.as<RangeSelectSyntax>().right); // lsb
            width = (down ? 1 : -1) * (int)(left->eval().integer().as<int64_t>().value() -
                    right->eval().integer().as<int64_t>().value());
            break;
        case SyntaxKind::AscendingRangeSelect:
        case SyntaxKind::DescendingRangeSelect:
            left = &bindAndPropagate(selector.as<RangeSelectSyntax>().left); // msb/lsb
            right = &bindAndPropagate(selector.as<RangeSelectSyntax>().right); // width
            width = int(right->eval().integer().as<int64_t>().value());
            break;
        default: THROW_UNREACHABLE;
    }
    return *compilation.emplace<SelectExpression>(
        compilation.getType(width, expr.type->isSigned(), expr.type->isFourState()),
        kind,
        expr,
        *left,
        *right,
        syntax
    );
}

Statement& Binder::bindReturnStatement(const ReturnStatementSyntax& syntax) const {
    auto stmtLoc = syntax.returnKeyword.location();
    const Symbol* subroutine = scope.asSymbol().findAncestor(SymbolKind::Subroutine);
    if (!subroutine) {
        compilation.addError(DiagCode::ReturnNotInSubroutine, stmtLoc);
        return badStmt(nullptr);
    }

    const auto& expr = Binder(*this).bindAssignmentLikeContext(*syntax.returnValue, stmtLoc,
                                                               *subroutine->as<SubroutineSymbol>().returnType);
    return *compilation.emplace<ReturnStatement>(syntax, &expr);
}

Statement& Binder::bindConditionalStatement(const ConditionalStatementSyntax& syntax) const {
    ASSERT(syntax.predicate.conditions.count() == 1);
    ASSERT(!syntax.predicate.conditions[0]->matchesClause);

    const auto& cond = Binder(*this).bindSelfDeterminedExpression(syntax.predicate.conditions[0]->expr);
    const auto& ifTrue = bindStatement(syntax.statement);
    const Statement* ifFalse = nullptr;
    if (syntax.elseClause)
        ifFalse = &bindStatement(syntax.elseClause->clause.as<StatementSyntax>());

    return *compilation.emplace<ConditionalStatement>(syntax, cond, ifTrue, ifFalse);
}

Statement& Binder::bindForLoopStatement(const ForLoopStatementSyntax&) const {
    // TODO: initializers need better handling

    // If the initializers here involve doing variable declarations, then the spec says we create
    // an implicit sequential block and do the declaration there.
    /*BumpAllocator& alloc = root.allocator();
    SequentialBinder& implicitInitBlock = *alloc.emplace<SequentialBinder>(*this);
    const auto& forVariable = syntax.initializers[0]->as<ForVariableDeclarationSyntax>();

    const auto& loopVar = *alloc.emplace<VariableSymbol>(forVariable.declarator.name, forVariable.type,
    implicitInitBlock, VariableLifetime::Automatic, false,
    &forVariable.declarator.initializer->expr);

    implicitInitBlock.setMember(loopVar);
    builder.add(implicitInitBlock);

    Binder binder(implicitInitBlock);
    const auto& stopExpr = binder.bindSelfDeterminedExpression(syntax.stopExpr);

    SmallVectorSized<const BoundExpression*, 2> steps;
    for (auto step : syntax.steps)
    steps.append(&binder.bindSelfDeterminedExpression(*step));

    const auto& statement = implicitInitBlock.bindStatement(syntax.statement);
    const auto& loop = allocate<BoundForLoopStatement>(syntax, stopExpr, steps.copy(compilation), statement);

    SmallVectorSized<const Statement*, 2> blockList;
    blockList.append(&allocate<BoundVariableDecl>(loopVar));
    blockList.append(&loop);

    implicitInitBlock.setBody(allocate<StatementList>(blockList.copy(compilation)));
    return allocate<BoundSequentialBlock>(implicitInitBlock);*/

    return badStmt(nullptr);
}

Statement& Binder::bindExpressionStatement(const ExpressionStatementSyntax& syntax) const {
    const auto& expr = Binder(*this).bindSelfDeterminedExpression(syntax.expr);
    return *compilation.emplace<ExpressionStatement>(syntax, expr);
}

bool Binder::checkOperatorApplicability(SyntaxKind op, SourceLocation location, Expression** operand) {
    if ((*operand)->bad())
        return false;

    const TypeSymbol* type = (*operand)->type;
    bool good;
    switch (op) {
        case SyntaxKind::UnaryPlusExpression:
        case SyntaxKind::UnaryMinusExpression:
        case SyntaxKind::UnaryLogicalNotExpression:
            good = type->kind == SymbolKind::IntegralType || type->kind == SymbolKind::RealType;
            break;
        default:
            good = type->kind == SymbolKind::IntegralType;
            break;
    }
    if (good)
        return true;

    auto& diag = compilation.addError(DiagCode::BadUnaryExpression, location);
    diag << type->toString();
    // TODO: source ranges for symbols / expressions
    //diag << (*operand)->syntax.sourceRange();
    return false;
}

bool Binder::checkOperatorApplicability(SyntaxKind op, SourceLocation location, Expression** lhs, Expression** rhs) {
    if ((*lhs)->bad() || (*rhs)->bad())
        return false;

    const TypeSymbol* lt = (*lhs)->type;
    const TypeSymbol* rt = (*rhs)->type;
    bool good;
    switch (op) {
        case SyntaxKind::AddExpression:
        case SyntaxKind::SubtractExpression:
        case SyntaxKind::MultiplyExpression:
        case SyntaxKind::DivideExpression:
        case SyntaxKind::PowerExpression:
        case SyntaxKind::LogicalAndExpression:
        case SyntaxKind::LogicalOrExpression:
        case SyntaxKind::LogicalImplicationExpression:
        case SyntaxKind::LogicalEquivalenceExpression:
        case SyntaxKind::LessThanEqualExpression:
        case SyntaxKind::LessThanExpression:
        case SyntaxKind::GreaterThanEqualExpression:
        case SyntaxKind::GreaterThanExpression:
        case SyntaxKind::EqualityExpression:
        case SyntaxKind::InequalityExpression:
        case SyntaxKind::WildcardEqualityExpression:
        case SyntaxKind::WildcardInequalityExpression:
        case SyntaxKind::CaseEqualityExpression:
        case SyntaxKind::CaseInequalityExpression:
            good = (lt->kind == SymbolKind::IntegralType || lt->kind == SymbolKind::RealType) &&
                   (rt->kind == SymbolKind::IntegralType || rt->kind == SymbolKind::RealType);
            break;
        default:
            good = lt->kind == SymbolKind::IntegralType && rt->kind == SymbolKind::IntegralType;
            break;
    }
    if (good)
        return true;

    auto& diag = compilation.addError(DiagCode::BadBinaryExpression, location);
    diag << lt->toString() << rt->toString();
    // TODO:
    //diag << (*lhs)->syntax.sourceRange();
    //diag << (*rhs)->syntax.sourceRange();
    return false;
}

bool Binder::propagateAssignmentLike(Expression& rhs, const TypeSymbol& lhsType) {
    // Integral case
    if (lhsType.width() > rhs.type->width()) {
        if (!lhsType.isReal() && !rhs.type->isReal()) {
            rhs.type = &compilation.getType(lhsType.width(), rhs.type->isSigned(), rhs.type->isFourState());
        }
        else {
            if (lhsType.width() > 32)
                rhs.type = &compilation.getRealType();
            else
                rhs.type = &compilation.getShortRealType();
        }
        rhs.propagateType(*rhs.type);
        return true;
    }
    return false;
}

const TypeSymbol& Binder::binaryOperatorResultType(const TypeSymbol* lhsType, const TypeSymbol* rhsType, bool forceFourState) {
    int width = std::max(lhsType->width(), rhsType->width());
    bool isSigned = lhsType->isSigned() && rhsType->isSigned();
    bool fourState = forceFourState || lhsType->isFourState() || rhsType->isFourState();

    if (lhsType->isReal() || rhsType->isReal()) {
        // spec says that RealTime and RealType are interchangeable, so we will just use RealType for
        // intermediate symbols
        // TODO: The spec is unclear for binary operators what to do if the operands are a shortreal and a larger
        // integral type. For the conditional operator it is clear that this case should lead to a shortreal, and
        // it isn't explicitly mentioned for other binary operators
        if (width >= 64)
            return compilation.getRealType();
        else
            return compilation.getShortRealType();
    }
    else {
        return compilation.getType(width, isSigned, fourState);
    }
}

InvalidExpression& Binder::badExpr(const Expression* expr) {
    return *compilation.emplace<InvalidExpression>(expr, compilation.getErrorType());
}

InvalidStatement& Binder::badStmt(const Statement* stmt) const {
    return *compilation.emplace<InvalidStatement>(stmt);
}

}