/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#include <libsolidity/ast/experimental/Type.h>
#include <libsolidity/ast/AST.h>
#include <libsolutil/Visitor.h>

#include <range/v3/view/drop_last.hpp>
#include <range/v3/view/zip.hpp>

#include <sstream>

using namespace std;
using namespace solidity;
using namespace solidity::frontend::experimental;

std::string frontend::experimental::canonicalTypeName(Type _type)
{
	return std::visit(util::GenericVisitor{
		[&](TypeConstant const& _type) {
			std::stringstream stream;
			auto printTypeArguments = [&]() {
				if (!_type.arguments.empty())
				{
					stream << "$";
					for (auto type: _type.arguments | ranges::views::drop_last(1))
						stream << canonicalTypeName(type) << "$";
					stream << canonicalTypeName(_type.arguments.back());
					stream << "$";
				}
			};
			std::visit(util::GenericVisitor{
				[&](Declaration const* _declaration) {
					printTypeArguments();
					if (auto const* typeDeclarationAnnotation = dynamic_cast<TypeDeclarationAnnotation const*>(&_declaration->annotation()))
						stream << *typeDeclarationAnnotation->canonicalName;
					else
						// TODO: canonical name
						stream << _declaration->name();
				},
				[&](BuiltinType _builtinType) {
					printTypeArguments();
					switch(_builtinType)
					{
					case BuiltinType::Type:
						stream << "type";
						break;
					case BuiltinType::Sort:
						stream << "sort";
						break;
					case BuiltinType::Void:
						stream << "void";
						break;
					case BuiltinType::Function:
						stream << "fun";
						break;
					case BuiltinType::Unit:
						stream << "unit";
						break;
					case BuiltinType::Pair:
						stream << "pair";
						break;
					case BuiltinType::Word:
						stream << "word";
						break;
					case BuiltinType::Integer:
						stream << "integer";
						break;
					}
				}
			}, _type.constructor);
			return stream.str();
		},
		[](TypeVariable const&)-> string {
			solAssert(false);
		},
	}, _type);
}

bool TypeClass::operator<(TypeClass const& _rhs) const
{
	return std::visit(util::GenericVisitor{
		[](BuiltinClass _left, BuiltinClass _right) { return _left < _right; },
		[](TypeClassDefinition const* _left, TypeClassDefinition const* _right) { return _left->id() < _right->id(); },
		[](BuiltinClass, TypeClassDefinition const*) { return true; },
		[](TypeClassDefinition const*, BuiltinClass) { return false; },
	}, declaration, _rhs.declaration);
}

bool TypeClass::operator==(TypeClass const& _rhs) const
{
	return std::visit(util::GenericVisitor{
		[](BuiltinClass _left, BuiltinClass _right) { return _left == _right; },
		[](TypeClassDefinition const* _left, TypeClassDefinition const* _right) { return _left->id() == _right->id(); },
		[](BuiltinClass, TypeClassDefinition const*) { return false; },
		[](TypeClassDefinition const*, BuiltinClass) { return false; },
	}, declaration, _rhs.declaration);
}

string TypeClass::toString() const
{
	return std::visit(util::GenericVisitor{
		[](BuiltinClass _class) -> string {
			switch(_class)
			{
			case BuiltinClass::Type:
				return "type";
			case BuiltinClass::Kind:
				return "kind";
			case BuiltinClass::Constraint:
				return "contraint";
			}
			solAssert(false);
		},
		[](TypeClassDefinition const* _declaration) { return _declaration->name(); },
	}, declaration);
}

bool Sort::operator==(Sort const& _rhs) const
{
	if (classes.size() != _rhs.classes.size())
		return false;
	for (auto [lhs, rhs]: ranges::zip_view(classes, _rhs.classes))
		if (lhs != rhs)
			return false;
	return true;
}

bool Sort::operator<=(Sort const& _rhs) const
{
	for (auto c: classes)
		if (!_rhs.classes.count(c))
			return false;
	return true;
}

Sort Sort::operator+(Sort const& _rhs) const
{
	Sort result { classes };
	result.classes += _rhs.classes;
	return result;
}


Sort Sort::operator-(Sort const& _rhs) const
{
	Sort result { classes };
	result.classes -= _rhs.classes;
	return result;
}

bool TypeConstant::operator<(TypeConstant const& _rhs) const
{
	if (constructor < _rhs.constructor)
		return true;
	if (_rhs.constructor < constructor)
		return false;
	solAssert(arguments.size() == _rhs.arguments.size());
	for(auto [lhs, rhs]: ranges::zip_view(arguments, _rhs.arguments))
	{
		if (lhs < rhs)
			return true;
		if (rhs < lhs)
			return false;
	}
	return false;
}