/*
 * Copyright 2010-2015 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#include <iomanip>
#include <tuple>
#include <algorithm>

#include "Logger.h"
#include "Options.h"
#include "Script.h"
#include "ScriptBind.h"
#include "Surface.h"
#include "ShaderDraw.h"
#include "ShaderMove.h"
#include "Exception.h"


namespace OpenXcom
{

////////////////////////////////////////////////////////////
//						arg definition
////////////////////////////////////////////////////////////
#define MACRO_QUOTE(...) __VA_ARGS__

#define MACRO_COPY_4(Func, Pos) \
	Func((Pos) + 0x0) \
	Func((Pos) + 0x1) \
	Func((Pos) + 0x2) \
	Func((Pos) + 0x3)
#define MACRO_COPY_16(Func, Pos) \
	MACRO_COPY_4(Func, (Pos) + 0x00) \
	MACRO_COPY_4(Func, (Pos) + 0x04) \
	MACRO_COPY_4(Func, (Pos) + 0x08) \
	MACRO_COPY_4(Func, (Pos) + 0x0C)
#define MACRO_COPY_64(Func, Pos) \
	MACRO_COPY_16(Func, (Pos) + 0x00) \
	MACRO_COPY_16(Func, (Pos) + 0x10) \
	MACRO_COPY_16(Func, (Pos) + 0x20) \
	MACRO_COPY_16(Func, (Pos) + 0x30)
#define MACRO_COPY_256(Func, Pos) \
	MACRO_COPY_64(Func, (Pos) + 0x00) \
	MACRO_COPY_64(Func, (Pos) + 0x40) \
	MACRO_COPY_64(Func, (Pos) + 0x80) \
	MACRO_COPY_64(Func, (Pos) + 0xC0)


////////////////////////////////////////////////////////////
//						proc definition
////////////////////////////////////////////////////////////
[[gnu::always_inline]]
static inline void addShade_h(int& reg, const int& var)
{
	const int newShade = (reg & 0xF) + var;
	if (newShade > 0xF)
	{
		// so dark it would flip over to another color - make it black instead
		reg = 0xF;
		return;
	}
	else if (newShade > 0)
	{
		reg = (reg & 0xF0) | newShade;
		return;
	}
	reg &= 0xF0;
	//prevent overflow to 0 or another color - make it white instead
	if (!reg || newShade < 0)
		reg = 0x01;
}

[[gnu::always_inline]]
static inline RetEnum mulAddMod_h(int& reg, const int& mul, const int& add, const int& mod)
{
	const int a = reg * mul + add;
	if (mod)
	{
		reg = (a % mod + mod) % mod;
		return RetContinue;
	}
	return RetError;
}

[[gnu::always_inline]]
static inline RetEnum wavegen_rect_h(int& reg, const int& period, const int& size, const int& max)
{
	if (period <= 0)
		return RetError;
	reg %= period;
	if (reg < 0)
		reg += period;
	if (reg > size)
		reg = 0;
	else
		reg = max;
	return RetContinue;
}

[[gnu::always_inline]]
static inline RetEnum wavegen_saw_h(int& reg, const int& period, const int& size, const int& max)
{
	if (period <= 0)
		return RetError;
	reg %= period;
	if (reg < 0)
		reg += period;
	if (reg > size)
		reg = 0;
	else if (reg > max)
		reg = max;
	return RetContinue;
}

[[gnu::always_inline]]
static inline RetEnum wavegen_tri_h(int& reg, const int& period, const int& size, const int& max)
{
	if (period <= 0)
		return RetError;
	reg %= period;
	if (reg < 0)
		reg += period;
	if (reg > size)
		reg = 0;
	else
	{
		if (reg > size/2)
			reg = size - reg;
		if (reg > max)
			reg = max;
	}
	return RetContinue;
}

[[gnu::always_inline]]
static inline RetEnum call_func_h(ScriptWorkerBase& c, FuncCommon func, const Uint8* d, ProgPos& p)
{
	auto t = p;
	auto r = func(c, d, t);
	p = t;
	return r;
}

[[gnu::always_inline]]
static inline RetEnum debug_log_h(ProgPos& p, int i, int j)
{
	if (Options::debug)
	{
		static int limit_count = 0;
		if (++limit_count < 500)
		{
			Logger log;
			log.get(LOG_DEBUG) << "Script debug log at " << std::hex << std::showbase << std::setw(8) << ((int)p) << " values: " << std::setw(8) << i << std::setw(8) << j;
		}
	}
	return RetContinue;
}


/**
 * Main macro defining all available operation in script engine.
 * @param IMPL macro function that access data. Take 3 args: Name, definition of operation and delcaration of it's arguments.
 */
#define MACRO_PROC_DEFINITION(IMPL) \
	/*	Name,		Implementation,													End excecution,				Args */ \
	IMPL(exit,		MACRO_QUOTE({													return RetEnd;		}),		(ScriptWorkerBase &)) \
	\
	IMPL(return,	MACRO_QUOTE({ c.ref<int>(RegI0) = Data0;						return RetEnd;		}),		(ScriptWorkerBase& c, int Data0)) \
	\
	IMPL(goto,		MACRO_QUOTE({ Prog = Label1;									return RetContinue; }),		(ScriptWorkerBase& c, ProgPos& Prog, ProgPos Label1)) \
	\
	IMPL(set,		MACRO_QUOTE({ Reg0 = Data1;										return RetContinue; }),		(ScriptWorkerBase& c, int& Reg0, int Data1)) \
	\
	IMPL(test_le,	MACRO_QUOTE({ Prog = (A <= B) ? LabelTrue : LabelFalse;			return RetContinue; }),		(ProgPos& Prog, int A, int B, ProgPos LabelTrue, ProgPos LabelFalse)) \
	IMPL(test_eq,	MACRO_QUOTE({ Prog = (A == B) ? LabelTrue : LabelFalse;			return RetContinue; }),		(ProgPos& Prog, int A, int B, ProgPos LabelTrue, ProgPos LabelFalse)) \
	\
	IMPL(swap,		MACRO_QUOTE({ std::swap(Reg0, Reg1);							return RetContinue; }),		(int& Reg0, int& Reg1)) \
	IMPL(add,		MACRO_QUOTE({ Reg0 += Data1;									return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(sub,		MACRO_QUOTE({ Reg0 -= Data1;									return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(mul,		MACRO_QUOTE({ Reg0 *= Data1;									return RetContinue; }),		(int& Reg0, int Data1)) \
	\
	IMPL(aggregate,	MACRO_QUOTE({ Reg0 = Reg0 + Data1 * Data2;						return RetContinue; }),		(int& Reg0, int Data1, int Data2)) \
	IMPL(offset,	MACRO_QUOTE({ Reg0 = Reg0 * Data1 + Data2;						return RetContinue; }),		(int& Reg0, int Data1, int Data2)) \
	IMPL(offsetmod,	MACRO_QUOTE({ return mulAddMod_h(Reg0, Mul1, Add2, Mod3);							}),		(int& Reg0, int Mul1, int Add2, int Mod3)) \
	\
	IMPL(div,		MACRO_QUOTE({ if (!Data1) return RetError; Reg0 /= Data1;		return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(mod,		MACRO_QUOTE({ if (!Data1) return RetError; Reg0 %= Data1;		return RetContinue; }),		(int& Reg0, int Data1)) \
	\
	IMPL(shl,		MACRO_QUOTE({ Reg0 <<= Data1;									return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(shr,		MACRO_QUOTE({ Reg0 >>= Data1;									return RetContinue; }),		(int& Reg0, int Data1)) \
	\
	IMPL(abs,			MACRO_QUOTE({ Reg0 = std::abs(Reg0);							return RetContinue; }),		(int& Reg0)) \
	IMPL(limit,			MACRO_QUOTE({ Reg0 = std::max(std::min(Reg0, Data2), Data1);	return RetContinue; }),		(int& Reg0, int Data1, int Data2)) \
	IMPL(limit_upper,	MACRO_QUOTE({ Reg0 = std::min(Reg0, Data1);						return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(limit_lower,	MACRO_QUOTE({ Reg0 = std::max(Reg0, Data1);						return RetContinue; }),		(int& Reg0, int Data1)) \
	\
	IMPL(wavegen_rect,	MACRO_QUOTE({ return wavegen_rect_h(Reg0, Period1, Size2, Max3);				}),		(int& Reg0, int Period1, int Size2, int Max3)) \
	IMPL(wavegen_saw,	MACRO_QUOTE({ return wavegen_saw_h(Reg0, Period1, Size2, Max3);					}),		(int& Reg0, int Period1, int Size2, int Max3)) \
	IMPL(wavegen_tri,	MACRO_QUOTE({ return wavegen_tri_h(Reg0, Period1, Size2, Max3);					}),		(int& Reg0, int Period1, int Size2, int Max3)) \
	\
	IMPL(get_color,		MACRO_QUOTE({ Reg0 = Data1 >> 4;							return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(set_color,		MACRO_QUOTE({ Reg0 = (Reg0 & 0xF) | (Data1 << 4);			return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(get_shade,		MACRO_QUOTE({ Reg0 = Data1 & 0xF;							return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(set_shade,		MACRO_QUOTE({ Reg0 = (Reg0 & 0xF0) | (Data1 & 0xF);			return RetContinue; }),		(int& Reg0, int Data1)) \
	IMPL(add_shade,		MACRO_QUOTE({ addShade_h(Reg0, Data1);						return RetContinue; }),		(int& Reg0, int Data1)) \
	\
	IMPL(call,			MACRO_QUOTE({ return call_func_h(c, func, d, p);								}),		(FuncCommon func, const Uint8* d, ScriptWorkerBase& c, ProgPos& p)) \
	\
	IMPL(debug_log,		MACRO_QUOTE({ return debug_log_h(p, Data1, Data2);								}),		(ProgPos& p, int Data1, int Data2)) \


////////////////////////////////////////////////////////////
//					function definition
////////////////////////////////////////////////////////////

namespace
{

/**
 * Macro returning name of function
 */
#define MACRO_FUNC_ID(id) Func_##id

/**
 * Macro used for creating functions from MACRO_PROC_DEFINITION
 */
#define MACRO_CREATE_FUNC(NAME, Impl, Args) \
	struct MACRO_FUNC_ID(NAME) \
	{ \
		[[gnu::always_inline]] \
		static RetEnum func Args \
			Impl \
	};

MACRO_PROC_DEFINITION(MACRO_CREATE_FUNC)

#undef MACRO_CREATE_FUNC


struct Func_test_eq_null
{
	[[gnu::always_inline]]
	static RetEnum func (ProgPos& Prog, std::nullptr_t, std::nullptr_t, ProgPos LabelTrue, ProgPos)
	{
		Prog = LabelTrue;
		return RetContinue;
	}
};

} //namespace

////////////////////////////////////////////////////////////
//					Proc_Enum definition
////////////////////////////////////////////////////////////

/**
 * Macro returning enum form ProcEnum
 */
#define MACRO_PROC_ID(id) Proc_##id

/**
 * Macro used for creating ProcEnum from MACRO_PROC_DEFINITION
 */
#define MACRO_CREATE_PROC_ENUM(NAME, ...) \
	MACRO_PROC_ID(NAME), \
	Proc_##NAME##_end = MACRO_PROC_ID(NAME) + helper::FuncGroup<MACRO_FUNC_ID(NAME)>::ver() - 1,

/**
 * Enum storing id of all avaliable operations in script engine
 */
enum ProcEnum : Uint8
{
	MACRO_PROC_DEFINITION(MACRO_CREATE_PROC_ENUM)
	Proc_EnumMax,
};

#undef MACRO_CREATE_PROC_ENUM

////////////////////////////////////////////////////////////
//					core loop function
////////////////////////////////////////////////////////////

/**
 * Core function in script engine used to executing scripts
 * @param i0 arg that is visible in script under name "i0"
 * @param i1 arg that is visible in script under name "i1"
 * @param proc array storing operation of script
 * @return Result of executing script
 */
static inline int scriptExe(int i0, int i1, ScriptWorkerBase& data, const Uint8* proc)
{
	data.ref<int>(RegI0) = i0;
	data.ref<int>(RegI1) = i1;
	ProgPos curr = ProgPos::Start;
	//--------------------------------------------------
	//			helper macros for this function
	//--------------------------------------------------
	#define MACRO_FUNC_ARRAY(NAME, ...) + helper::FuncGroup<MACRO_FUNC_ID(NAME)>::FuncList{}
	#define MACRO_FUNC_ARRAY_LOOP(POS) \
		case (POS): \
		{ \
			using currType = helper::GetType<func, POS>; \
			const auto p = proc + (int)curr; \
			curr += currType::offset; \
			const auto ret = currType::func(data, p, curr); \
			if (ret != RetContinue) \
			{ \
				if (ret == RetEnd) \
					goto endLabel; \
				else \
				{ \
					curr += - currType::offset - 1; \
					goto errorLabel; \
				} \
			} \
			else \
				continue; \
		}
	//--------------------------------------------------

	using func = decltype(MACRO_PROC_DEFINITION(MACRO_FUNC_ARRAY));

	while (true)
	{
		switch (proc[(int)curr++])
		{
		MACRO_COPY_256(MACRO_FUNC_ARRAY_LOOP, 0)
		}
	}

	//--------------------------------------------------
	//			removing helper macros
	//--------------------------------------------------
	#undef MACRO_FUNC_ARRAY_LOOP
	#undef MACRO_FUNC_ARRAY
	//--------------------------------------------------

	endLabel:
	return data.ref<int>(RegI0);
	errorLabel:
	static int bugCount = 0;
	if (++bugCount < 100)
	{
		Log(LOG_ERROR) << "Invaild script operation for OpId: " << std::hex << std::showbase << (int)proc[(int)curr] <<" at "<< (int)curr;
	}
	return 0;
}


////////////////////////////////////////////////////////////
//						Script class
////////////////////////////////////////////////////////////

namespace
{

/**
 * struct used to bliting script
 */
struct ScriptReplace
{
	static inline void func(Uint8& dest, const Uint8& src, ScriptWorkerBase& ref, const Uint8* proc)
	{
		if (src)
		{
			const int s = scriptExe(src, dest, ref, proc);
			if (s) dest = s;
		}
	}
};

} //namespace



/**
 * Bliting one surface to another using script.
 * @param src source surface.
 * @param dest destination surface.
 * @param x x offset of source surface.
 * @param y y offset of source surface.
 */
void ScriptWorkerBlit::executeBlit(Surface* src, Surface* dest, int x, int y, int shade, bool half)
{
	ShaderMove<Uint8> srcShader(src, x, y);
	if (half)
	{
		GraphSubset g = srcShader.getDomain();
		g.beg_x = g.end_x/2;
		srcShader.setDomain(g);
	}
	if (_proc)
		ShaderDraw<ScriptReplace>(ShaderSurface(dest, 0, 0), srcShader, ShaderScalar(*this), ShaderScalar(_proc));
	else
		ShaderDraw<helper::StandardShade>(ShaderSurface(dest, 0, 0), srcShader, ShaderScalar(shade));
}

/**
 * Execute script with two arguments.
 * @param i0 arg that is visible in script under name "i0"
 * @param i1 arg that is visible in script under name "i1"
 * @return Result value from script.
 */
int ScriptWorkerBase::executeBase(const Uint8* proc, int i0, int i1)
{
	if (proc)
	{
		return scriptExe(i0, i1, *this, proc);
	}
	else
	{
		return i0;
	}
}

////////////////////////////////////////////////////////////
//					Helper functions
////////////////////////////////////////////////////////////

namespace
{

/**
 * Test for validaty of arguments.
 */
bool validOverloadProc(const ScriptRange<ScriptRange<ArgEnum>>& overload)
{
	for (auto& p : overload)
	{
		for (auto& pp : p)
		{
			if (pp == ArgInvalid)
			{
				return false;
			}
		}
	}
	return true;
}

/**
 * Display arguments
 */
std::string displayOverloadProc(const ScriptParserBase* spb, const ScriptRange<ScriptRange<ArgEnum>>& overload)
{
	std::string result = "";
	for (auto& p : overload)
	{
		if (p)
		{
			auto type = *p.begin();
			result += "[";
			result += spb->getTypePrefix(type);
			result += spb->getTypeName(type).toString();
			result += "] ";
		}
	}
	if (!result.empty())
	{
		result.pop_back();
	}
	return result;
}

/**
 * Accept all arguments.
 */
int overloadBuildinProc(const ScriptProcData& spd, const ScriptRefData* begin, const ScriptRefData* end)
{
	return 1;
}

/**
 * Reject all arguments.
 */
int overloadInvalidProc(const ScriptProcData& spd, const ScriptRefData* begin, const ScriptRefData* end)
{
	return 0;
}

/**
 * Verify arguments.
 */
int overloadCustomProc(const ScriptProcData& spd, const ScriptRefData* begin, const ScriptRefData* end)
{
	auto tempSorce = 255;
	auto curr = begin;
	for (auto& currOver : spd.overloadArg)
	{
		if (curr == end)
		{
			return 0;
		}
		const auto size = currOver.size();
		if (size)
		{
			if (*curr)
			{
				int oneArgTempScore = 0;
				for (auto& o : currOver)
				{
					oneArgTempScore = std::max(oneArgTempScore, ArgCompatible(o, curr->type, size - 1));
				}
				tempSorce = std::min(tempSorce, oneArgTempScore);
			}
			++curr;
		}
	}
	if (curr != end)
	{
		return 0;
	}
	return tempSorce;
}
/**
 * Helper choosing corrct overload function to call.
 */
bool callOverloadProc(ParserWriter& ph, const ScriptRange<ScriptProcData>& proc, const SelectedToken* begin, const SelectedToken* end)
{
	if (!proc)
	{
		return false;
	}
	if (std::distance(begin, end) > ScriptMaxArg)
	{
		return false;
	}

	ScriptRefData typeArg[ScriptMaxArg];
	auto beginArg = std::begin(typeArg);
	auto endArg = std::transform(begin, end, beginArg, [&](const SelectedToken& st){ return ph.getReferece(st); });

	int bestSorce = 0;
	const ScriptProcData* bestValue = nullptr;
	for (auto& p : proc)
	{
		int tempSorce = p.overload(p, beginArg, endArg);
		if (tempSorce)
		{
			if (tempSorce == bestSorce)
			{
				bestValue = nullptr;
			}
			else if (tempSorce > bestSorce)
			{
				bestSorce = tempSorce;
				bestValue = &p;
			}
		}
	}
	if (bestSorce)
	{
		if (bestValue)
		{
			if ((*bestValue)(ph, begin, end) == false)
			{
				Log(LOG_ERROR) << "Error in maching arguments for operator '" + proc.begin()->name.toString() + "'";
				return false;
			}
			else
			{
				return true;
			}
		}
		else
		{
			Log(LOG_ERROR) << "Conflicting overloads for operator '" + proc.begin()->name.toString() + "'";
			return false;
		}
	}
	else
	{
		Log(LOG_ERROR) << "Can't match overload for operator '" + proc.begin()->name.toString() + "'";
		return false;
	}
}

/**
 * Helper used to parse line for build in function.
 */
template<Uint8 procId, typename FuncGroup>
bool parseBuildinProc(const ScriptProcData& spd, ParserWriter& ph, const SelectedToken* begin, const SelectedToken* end)
{
	auto opPos = ph.pushProc(procId);
	int ver = FuncGroup::parse(ph, begin, end);
	if (ver >= 0)
	{
		ph.updateProc(opPos, ver);
		return true;
	}
	else
	{
		return false;
	}
}

/**
 * Helper used to parse line for custom functions.
 */
bool parseCustomProc(const ScriptProcData& spd, ParserWriter& ph, const SelectedToken* begin, const SelectedToken* end)
{
	using argFunc = typename helper::ArgSelector<FuncCommon>::type;
	using argRaw = typename helper::ArgSelector<const Uint8*>::type;
	static_assert(helper::FuncGroup<Func_call>::ver() == argRaw::ver(), "Invalid size");
	static_assert(std::is_same<helper::GetType<helper::FuncGroup<Func_call>, 0>, argFunc>::value, "Invalid first argument");
	static_assert(std::is_same<helper::GetType<helper::FuncGroup<Func_call>, 1>, argRaw>::value, "Invalid second argument");

	auto opPos = ph.pushProc(Proc_call);

	auto funcPos = ph.pushReserved<FuncCommon>();
	auto argPosBegin = ph.getCurrPos();

	auto argType = spd.parserArg(ph, begin, end);

	if (argType < 0)
	{
		return false;
	}

	auto argPosEnd = ph.getCurrPos();
	ph.updateReserved<FuncCommon>(funcPos, spd.parserGet(argType));

	size_t diff = ph.getDiffPos(argPosBegin, argPosEnd);
	for (int i = 0; i < argRaw::ver(); ++i)
	{
		size_t off = argRaw::offset(i);
		if (off >= diff)
		{
			//aligin proc to fit fixed size.
			ph.push(off-diff);
			ph.updateProc(opPos, i);
			return true;
		}
	}
	return false;
}

constexpr size_t ConditionSize = 6;
const ScriptRef ConditionNames[ConditionSize] =
{
	ScriptRef{ "eq" }, ScriptRef{ "neq" },
	ScriptRef{ "le" }, ScriptRef{ "gt" },
	ScriptRef{ "ge" }, ScriptRef{ "lt" },
};

constexpr size_t ConditionSpecialSize = 2;
const ScriptRef ConditionSpecNames[ConditionSpecialSize] =
{
	ScriptRef{ "or" },
	ScriptRef{ "and" },
};


/**
 * Helper used of condintion opearations.
 */
bool parseConditionImpl(ParserWriter& ph, int truePos, int falsePos, const SelectedToken* begin, const SelectedToken* end)
{
	constexpr size_t TempArgsSize = 4;

	if (std::distance(begin, end) != 3)
	{
		Log(LOG_ERROR) << "Invaild length of condition arguments";
		return false;
	}

	SelectedToken conditionArgs[TempArgsSize] =
	{
		begin[1],
		begin[2],
		{ TokenBuildinLabel, truePos, }, //success
		{ TokenBuildinLabel, falsePos, }, //failure
	};

	bool equalFunc = false;
	size_t i = 0;
	for (; i < ConditionSize; ++i)
	{
		if (begin[0] == ConditionNames[i])
		{
			if (i < 2) equalFunc = true;
			if (i & 1) std::swap(conditionArgs[2], conditionArgs[3]); //negate condition result
			if (i >= 4) std::swap(conditionArgs[0], conditionArgs[1]); //swap condition args
			break;
		}
	}
	if (i == ConditionSize)
	{
		Log(LOG_ERROR) << "Unknown condition: '" + begin[0].toString() + "'";
		return false;
	}

	const auto proc = ph.parser.getProc(ScriptRef{ equalFunc ? "test_eq" : "test_le" });
	if (callOverloadProc(ph, proc, std::begin(conditionArgs), std::end(conditionArgs)) == false)
	{
		Log(LOG_ERROR) << "Unsupported operator: '" + begin[0].toString() + "'";
		return false;
	}

	return true;
}

/**
 * Parse `or` or `and` conditions.
 */
bool parseFullConditionImpl(ParserWriter& ph, int falsePos, const SelectedToken* begin, const SelectedToken* end)
{
	if (std::distance(begin, end) <= 1)
	{
		Log(LOG_ERROR) << "Invaild length of condition arguments";
		return false;
	}
	const auto truePos = ph.addLabel();
	const auto orFunc = begin[0] == ConditionSpecNames[0];
	const auto andFunc = begin[0] == ConditionSpecNames[1];
	if (orFunc || andFunc)
	{
		++begin;
		for (; std::distance(begin, end) > 3; begin += 3)
		{
			auto temp = ph.addLabel();
			if (orFunc)
			{
				if (!parseConditionImpl(ph, truePos, temp, begin, begin + 3))
				{
					return false;
				}
			}
			else
			{
				if (!parseConditionImpl(ph, temp, falsePos, begin, begin + 3))
				{
					return false;
				}
			}
			ph.setLabel(temp, ph.getCurrPos());
		}
	}
	if (!parseConditionImpl(ph, truePos, falsePos, begin, end))
	{
		return false;
	}

	ph.setLabel(truePos, ph.getCurrPos());
	return true;
}

/**
 * Parser of `if` operation.
 */
bool parseIf(const ScriptProcData& spd, ParserWriter& ph, const SelectedToken* begin, const SelectedToken* end)
{
	ParserWriter::Block block = { BlockIf, ph.addLabel(), ph.addLabel() };
	ph.codeBlocks.push_back(block);

	return parseFullConditionImpl(ph, block.nextLabel, begin, end);
}

/**
 * Parser of `else` operation.
 */
bool parseElse(const ScriptProcData& spd, ParserWriter& ph, const SelectedToken* begin, const SelectedToken* end)
{
	if (ph.codeBlocks.empty() || ph.codeBlocks.back().type != BlockIf)
	{
		Log(LOG_ERROR) << "Unexpected 'else'";
		return false;
	}

	ParserWriter::Block& block = ph.codeBlocks.back();

	ph.pushProc(Proc_goto);
	ph.pushLabel(block.finalLabel);

	ph.setLabel(block.nextLabel, ph.getCurrPos());
	if (std::distance(begin, end) == 0)
	{
		block.nextLabel = block.finalLabel;
		block.type = BlockElse;
		return true;
	}
	else
	{
		block.nextLabel = ph.addLabel();
		return parseFullConditionImpl(ph, block.nextLabel, begin, end);
	}
}

/**
 * Parser of `end` operation.
 */
bool parseEnd(const ScriptProcData& spd, ParserWriter& ph, const SelectedToken* begin, const SelectedToken* end)
{
	if (ph.codeBlocks.empty())
	{
		Log(LOG_ERROR) << "Unexpected 'end'";
		return false;
	}
	if (std::distance(begin, end) != 0)
	{
		Log(LOG_ERROR) << "Unexpected symbols after 'end'";
		return false;
	}

	ParserWriter::Block block = ph.codeBlocks.back();
	ph.codeBlocks.pop_back();

	if (block.nextLabel != block.finalLabel)
	{
		ph.setLabel(block.nextLabel, ph.getCurrPos());
	}
	ph.setLabel(block.finalLabel, ph.getCurrPos());
	return true;
}

/**
 * Parser of `var` operation that define local variables.
 */
bool parseVar(const ScriptProcData& spd, ParserWriter& ph, const SelectedToken* begin, const SelectedToken* end)
{
	if (ph.codeBlocks.size() > 0)
	{
		Log(LOG_ERROR) << "Can't define variables in code blocks";
		return false;
	}

	auto spec = ArgSpecVar;
	if (begin != end)
	{
		if (*begin == ScriptRef{ "ptr" })
		{
			spec = spec | ArgSpecPtr;
			++begin;
		}
		else if (*begin == ScriptRef{ "ptre" })
		{
			spec = spec | ArgSpecPtrE;
			++begin;
		}
	}
	auto size = std::distance(begin, end);
	if (size < 2 || 3 < size)
	{
		Log(LOG_ERROR) << "Invaild length of 'var' definition";
		return false;
	}

	// adding new custom variables of type selected type.
	auto type_curr = ph.parser.getType(*begin);
	if (type_curr)
	{
		if (type_curr->size == 0 && !(spec & ArgSpecPtr))
		{
			Log(LOG_ERROR) << "Can't create variable of type '" << begin->toString() << "'";
			return false;
		}

		++begin;
		if (begin->getType() != TokenSymbol || begin->find('.') != std::string::npos || ph.addReg(*begin, ArgSpec(type_curr->type, spec)) == false)
		{
			Log(LOG_ERROR) << "Invalid variable name '" << begin->toString() << "'";
			return false;
		}
		if (size == 3)
		{
			const auto proc = ph.parser.getProc(ScriptRef{ "set" });
			return callOverloadProc(ph, proc, begin, end);
		}
		return true;
	}
	else
	{
		Log(LOG_ERROR) << "Invalid type '" << begin->toString() << "'";
		return false;
	}
}

/**
 * Dummy operation.
 */
bool parseDummy(const ScriptProcData& spd, ParserWriter& ph, const SelectedToken* begin, const SelectedToken* end)
{
	Log(LOG_ERROR) << "Reserved operation for future use";
	return false;
}

/**
 * Add new value to sorted vector by name.
 * @param vec Vector with data.
 * @param name Name of new data.
 * @param value Data to add.
 */
template<typename R>
void addSortHelper(std::vector<R>& vec, R value)
{
	vec.push_back(value);
	std::sort(vec.begin(), vec.end(), [](const R& a, const R& b) { return ScriptRef::compare(a.name, b.name) < 0; });
}

/**
 * Get bound of value, upper od lower based on template parameter.
 * @param begin begin of sorted range.
 * @param end end of sorted range.
 * @param prefix First part of name.
 * @param postfix Second part of name.
 * @return Finded iterator or end iterator.
 */
template<bool upper, typename R>
R* boundSortHelper(R* begin, R* end, ScriptRef prefix, ScriptRef postfix = {})
{
	constexpr int limit = upper ? 1 : 0;
	if (postfix)
	{
		const auto size = prefix.size();
		const auto total_size = size + postfix.size();
		return std::partition_point(begin, end,
			[&](const R& a)
			{
				const auto curr = a.name.size();
				if (curr < total_size)
				{
					return true;
				}
				else if (curr == total_size)
				{
					const auto comp  = ScriptRef::compare(a.name.substr(0, size), prefix);
					return comp < 0 || (comp == 0 && ScriptRef::compare(a.name.substr(size), postfix) < limit);
				}
				else
				{
					return false;
				}
			}
		);
	}
	else
	{
		return std::partition_point(begin, end, [&](const R& a){ return ScriptRef::compare(a.name, prefix) < limit; });
	}
}

/**
 * Helper function finding data by name (that can be merge from two parts).
 * @param begin begin of sorted range.
 * @param end end of sorted range.
 * @param prefix First part of name.
 * @param postfix Second part of name.
 * @return Finded data or null.
 */
template<typename R>
R* findSortHelper(R* begin, R* end, ScriptRef prefix, ScriptRef postfix = {})
{
	auto f = boundSortHelper<false>(begin, end, prefix, postfix);
	if (f != end)
	{
		if (postfix)
		{
			const auto size = prefix.size();
			if (f->name.substr(0, size) == prefix && f->name.substr(size) == postfix)
			{
				return &*f;
			}
		}
		else
		{
			if (f->name == prefix)
			{
				return &*f;
			}
		}
	}
	return nullptr;
}

/**
 * Helper function finding data by name (that can be merge from two parts).
 * @param vec Vector with values.
 * @param prefix First part of name.
 * @param postfix Second part of name.
 * @return Finded data or null.
 */
template<typename R>
const R* findSortHelper(const std::vector<R>& vec, ScriptRef prefix, ScriptRef postfix = {})
{
	return findSortHelper(vec.data(), vec.data() + vec.size(), prefix, postfix);
}

/**
 * Helper function finding data by name (that can be merge from two parts).
 * @param vec Vector with values.
 * @param prefix First part of name.
 * @param postfix Second part of name.
 * @return Finded data or null.
 */
template<typename R>
R* findSortHelper(std::vector<R>& vec, ScriptRef prefix, ScriptRef postfix = {})
{
	return findSortHelper(vec.data(), vec.data() + vec.size(), prefix, postfix);
}

/**
 * Calculate space used by reg of that type.
 * @param parser
 * @param type
 * @return Size in bytes.
 */
size_t getRegSize(const ScriptParserBase& parser, ArgEnum type)
{
	auto t = parser.getType(type);
	if (t == nullptr)
	{
		return 0;
	}
	else
	{
		return ArgIsPtr(type) ? sizeof(void*) : t->size;
	}
}

/**
 * Add new string to colection and return reference to it.
 * @param list List where strings are stored.
 * @param s New string to add.
 * @return Reference to strored string.
 */
ScriptRef addString(std::vector<std::vector<char>>& list, const std::string& s)
{
	std::vector<char> refData;
	refData.assign(s.begin(), s.end());
	ScriptRef ref{ refData.data(), refData.data() + refData.size() };

	//we need use char vector becasue its guarante that pointer in ref will not get invalidated when names list grown.
	list.push_back(std::move(refData));
	return ref;
}

} //namespace

////////////////////////////////////////////////////////////
//					ScriptRef class
////////////////////////////////////////////////////////////

/**
 * Function extracting token form range
 * @param excepted what token type we expecting now
 * @return extracted token
 */
SelectedToken ScriptRef::getNextToken(TokenEnum excepted)
{
	//groups of different types of ASCII characters
	const Uint8 none = 1;
	const Uint8 spec = 2;
	const Uint8 digit = 3;
	const Uint8 charHex = 4;
	const Uint8 charRest = 5;
	const Uint8 digitSign = 6;

	//array storing data about every ASCII character
	static Uint8 charDecoder[256] = { 0 };
	static bool init = true;
	if (init)
	{
		init = false;
		for(int i = 0; i < 256; ++i)
		{
			if (i == '#' || isspace(i))	charDecoder[i] = none;
			if (i == ':' || i == ';')	charDecoder[i] = spec;

			if (i == '+' || i == '-')	charDecoder[i] = digitSign;
			if (i >= '0' && i <= '9')	charDecoder[i] = digit;
			if (i >= 'A' && i <= 'F')	charDecoder[i] = charHex;
			if (i >= 'a' && i <= 'f')	charDecoder[i] = charHex;

			if (i >= 'G' && i <= 'Z')	charDecoder[i] = charRest;
			if (i >= 'g' && i <= 'z')	charDecoder[i] = charRest;
			if (i == '_' || i == '.')	charDecoder[i] = charRest;
		}
	}

	//find first nowithe character.
	bool coment = false;
	for(; _begin != _end; ++_begin)
	{
		const char c = *_begin;
		if (coment)
		{
			if (c == '\n')
			{
				coment = false;
			}
			continue;
		}
		else if (isspace(c))
		{
			continue;
		}
		else if (c == '#')
		{
			coment = true;
			continue;
		}
		break;
	}
	if (_begin == _end)
	{
		return SelectedToken{ };
	}

	auto type = TokenNone;
	auto begin = _begin;
	bool hex = false;
	int off = 0;

	for (; _begin != _end; ++_begin, ++off)
	{
		const Uint8 c = *_begin;
		const Uint8 decode = charDecoder[c];

		//end of string
		if (decode == none)
		{
			break;
		}
		else if (decode == spec)
		{
			if (c == ':')
			{
				//colon start new token, skip if we are in another one
				if (type != TokenNone)
					break;

				++_begin;
				type = excepted == TokenColon ? TokenColon : TokenInvaild;
				break;
			}
			else if (c == ';')
			{
				//semicolon start new token, skip if we are in another one
				if (type != TokenNone)
					break;
				//semicolon wait for his turn, returning empty token
				if (excepted != TokenSemicolon)
					break;

				++_begin;
				type = TokenSemicolon;
				break;
			}
			else
			{
				type = TokenInvaild;
				break;
			}
		}

		switch (type)
		{
		//begin of string
		case TokenNone:
			switch(decode)
			{
			//start of number
			case digitSign:
				--off; //skipping +- sign
			case digit:
				hex = c == '0'; //expecting hex number
				type = TokenNumber;
				continue;

			//start of symbol
			case charHex:
			case charRest:
				type = TokenSymbol;
				continue;
			}
			break;

		//middle of number
		case TokenNumber:
			switch (decode)
			{
			case charRest:
				if (off != 1) break;
				if (c != 'x' && c != 'X') break; //X in "0x1"
			case charHex:
				if (!hex) break;
			case digit:
				if (off == 0) hex = c == '0'; //expecting hex number
				continue;
			}
			break;

		//middle of symbol
		case TokenSymbol:
			switch (decode)
			{
			case charRest:
			case charHex:
			case digit:
				continue;
			}
			break;
		default:
			break;
		}
		//when decode == 0 or we find unexpected char we should end there
		type = TokenInvaild;
		break;
	}
	auto end = _begin;
	return SelectedToken{ type, ScriptRef{ begin, end } };
}

////////////////////////////////////////////////////////////
//					ParserWriter class
////////////////////////////////////////////////////////////

/**
 * Constructor
 * @param scr parsed script
 * @param proc member pointer accessing script operations
 * @param procRefData member pointer accessing script data
 * @param procList all available operations for script
 * @param ref all available data for script
 */
ParserWriter::ParserWriter(
		Uint8 regUsed,
		ScriptContainerBase& c,
		const ScriptParserBase& d) :
	container(c),
	parser(d),
	refListCurr(),
	refLabelsUses(),
	refLabelsList(),
	regIndexUsed(regUsed),
	constIndexUsed(-1)
{

}

/**
 * Function finalizing parsing of script
 */
void ParserWriter::relese()
{
	pushProc(0);
	for (auto& p : refLabelsUses)
	{
		updateReserved<ProgPos>(p.first, refLabelsList[p.second]);
	}
}

/**
 * Returns reference based on name.
 * @param s name of referece.
 * @return referece data.
 */
ScriptRefData ParserWriter::getReferece(const ScriptRef& s) const
{
	auto ptr = findSortHelper(refListCurr, s);
	if (ptr == nullptr)
	{
		ptr = parser.getRef(s);
	}
	if (ptr == nullptr)
	{
		return ScriptRefData{ };
	}
	return *ptr;
}

/**
 * Add new referece definition.
 * @param s Name of referece.
 * @param data Data of reference.
 * @return pointer to new created reference.
 */
ScriptRefData ParserWriter::addReferece(const ScriptRefData& data)
{
	addSortHelper(refListCurr, data);
	return data;
}

/**
 * Get current position in proc vector.
 * @return Position in proc vector.
 */
ProgPos ParserWriter::getCurrPos() const
{
	return static_cast<ProgPos>(container._proc.size());
}

/**
 * Get distance betwean two positions in proc vector.
 */
size_t ParserWriter::getDiffPos(ProgPos begin, ProgPos end) const
{
	if (begin > end)
	{
		throw Exception("Invaild ProgPos distance");
	}
	return static_cast<size_t>(end) - static_cast<size_t>(begin);
}

/**
 * Push zeros to fill empty space.
 * @param s Size of empty space.
 */
ProgPos ParserWriter::push(size_t s)
{
	auto curr = getCurrPos();
	container._proc.insert(container._proc.end(), s, 0);
	return static_cast<ProgPos>(curr);
}

/**
 * Update part of proc vector.
 * @param pos position to update.
 * @param data data to write.
 * @param s size of data.
 */
void ParserWriter::update(ProgPos pos, void* data, size_t s)
{
	memcpy(&container._proc[static_cast<size_t>(pos)], data, s);
}

/**
 * Push custom value on proc vector.
 * @param p pointer to value.
 * @param size size of value.
 */
void ParserWriter::pushValue(ScriptValueData v)
{
	update(push(v.size), &v.data, v.size);
}

/**
 * Pushing proc operation id on proc vector.
 */
ParserWriter::ReservedPos<ParserWriter::ProcOp> ParserWriter::pushProc(Uint8 procId)
{
	auto curr = getCurrPos();
	container._proc.push_back(procId);
	return { curr };
}

/**
 * Updating previosoly added proc operation id.
 * @param pos Position of operation.
 * @param procOffset Offset value.
 */
void ParserWriter::updateProc(ReservedPos<ProcOp> pos, int procOffset)
{
	container._proc[static_cast<size_t>(pos.getPos())] += procOffset;
}

/**
 * Try pushing label arg on proc vector. Can't use this to create loop back label.
 * @param s name of label.
 * @return true if label was succefuly added.
 */
bool ParserWriter::pushLabelTry(const ScriptRef& s)
{
	ScriptRefData pos = getReferece(s);
	if (!pos)
	{
		pos = addReferece({ s, ArgLabel, addLabel() });
	}
	if (pos.type != ArgLabel && refLabelsList[pos.value.getValue<int>()] != ProgPos::Unknown)
		return false;

	pushLabel(pos.value.getValue<int>());
	return true;
}

/**
 * Push label arg to proc vector.
 * @param index
 */
void ParserWriter::pushLabel(int index)
{
	refLabelsUses.push_back(std::make_pair(pushReserved<ProgPos>(), index));
}
/**
 * Create new label definition for proc vector.
 * @return index of new label.
 */
int ParserWriter::addLabel()
{
	int pos = refLabelsList.size();
	refLabelsList.push_back(ProgPos::Unknown);
	return pos;
}

/**
 * Setting offset of label on proc vector.
 * @param s name of label
 * @param offset set value where label is pointing
 * @return true if operation success
 */
bool ParserWriter::setLabel(const ScriptRef& s, ProgPos offset)
{
	ScriptRefData pos = getReferece(s);
	if (!pos)
	{
		pos = addReferece({ s, ArgLabel, addLabel() });
	}
	if (pos.type != ArgLabel || refLabelsList[pos.value.getValue<int>()] != ProgPos::Unknown)
		return false;

	return setLabel(pos.value.getValue<int>(), offset);
}

/**
 * Setting offset of label on proc vector.
 * @param index index of label.
 * @param offset set value where label is pointing
 * @return true if operation success
 */
bool ParserWriter::setLabel(int index, ProgPos offset)
{
	if (refLabelsList[index] != ProgPos::Unknown)
		return false;
	refLabelsList[index] = offset;
	return true;
}

/**
 * Try pushing data arg on proc vector.
 * @param s name of data
 * @return true if data exists and is valid
 */
bool ParserWriter::pushConstTry(const ScriptRef& s, ArgEnum type)
{
	ScriptRefData pos = getReferece(s);
	if (pos && pos.type == type && !ArgIsReg(pos.type))
	{
		pushValue(pos.value);
		return true;
	}
	return false;
}

/**
 * Try pushing const arg on proc vector.
 * @param s name of const
 * @return true if const exists and is valid
 */
bool ParserWriter::pushNumberTry(const ScriptRef& s)
{
	auto str = s.toString();
	int value = 0;
	size_t offset = 0;
	std::stringstream ss(str);
	if (str[0] == '-' || str[0] == '+')
		offset = 1;
	if (str.size() > 2 + offset && str[offset] == '0' && (str[offset + 1] == 'x' || str[offset + 1] == 'X'))
		ss >> std::hex;
	if (!(ss >> value))
		return false;

	pushValue(value);
	return true;
}

/**
 * Try pushing reg arg on proc vector.
 * @param s name of reg
 * @return true if reg exists and is valid
 */
bool ParserWriter::pushRegTry(const ScriptRef& s, ArgEnum type)
{
	ScriptRefData pos = getReferece(s);
	type = ArgSpec(type, ArgSpecReg);
	if (pos && ArgCompatible(type, pos.type, 0) && pos.value.getValue<RegEnum>() != RegInvaild)
	{
		pushValue(static_cast<Uint8>(pos.value.getValue<RegEnum>()));
		return true;
	}
	return false;
}

/**
 * Add new reg arg definition.
 * @param s name of reg
 * @param type type of reg
 * @return true if reg exists and is valid
 */
bool ParserWriter::addReg(const ScriptRef& s, ArgEnum type)
{
	ScriptRefData pos = getReferece(s);
	type = ArgSpec(type, ArgSpecReg);
	if (pos)
	{
		return false;
	}
	auto size = getRegSize(parser, type);
	if (size == 0)
	{
		return false;
	}
	if (regIndexUsed + size > ScriptMaxReg)
	{
		return false;
	}
	ScriptRefData data = { s, type, static_cast<RegEnum>(regIndexUsed) };
	regIndexUsed += size;
	addReferece(data);
	return true;
}

////////////////////////////////////////////////////////////
//				ScriptParserBase class
////////////////////////////////////////////////////////////

/**
 * Constructor.
 */
ScriptParserBase::ScriptParserBase(ScriptGlobal* shared, const std::string& name, const std::string& firstArg, const std::string& secondArg) : _shared{ shared }, _regUsed{ RegMax }, _name{ name }
{
	//--------------------------------------------------
	//					op_data init
	//--------------------------------------------------
	#define MACRO_ALL_INIT(NAME, ...) \
		addParserBase(#NAME, nullptr, helper::FuncGroup<MACRO_FUNC_ID(NAME)>::overloadType(), &parseBuildinProc<MACRO_PROC_ID(NAME), helper::FuncGroup<MACRO_FUNC_ID(NAME)>>, nullptr, nullptr);

	MACRO_PROC_DEFINITION(MACRO_ALL_INIT)

	#undef MACRO_ALL_INIT

	addParserBase("if", &overloadBuildinProc, {}, &parseIf, nullptr, nullptr);
	addParserBase("else", &overloadBuildinProc, {}, &parseElse, nullptr, nullptr);
	addParserBase("end", &overloadBuildinProc, {}, &parseEnd, nullptr, nullptr);
	addParserBase("var", &overloadBuildinProc, {}, &parseVar, nullptr, nullptr);
	addParserBase("loop", &overloadBuildinProc, {}, &parseDummy, nullptr, nullptr);
	addParserBase("break", &overloadBuildinProc, {}, &parseDummy, nullptr, nullptr);
	addParserBase("continue", &overloadBuildinProc, {}, &parseDummy, nullptr, nullptr);

	addParser<helper::FuncGroup<Func_test_eq_null>>("test_eq");

	if (!firstArg.empty())
	{
		addStandartReg(firstArg, RegI0);
	}
	if (!secondArg.empty())
	{
		addStandartReg(secondArg, RegI1);
	}

	addType<int>("int");

	auto labelName = addNameRef("label");
	auto nullName = addNameRef("null");

	addSortHelper(_typeList, { labelName, ArgLabel, 0 });
	addSortHelper(_typeList, { nullName, ArgNull, 0 });
	addSortHelper(_refList, { nullName, ArgNull, {} });
}

/**
 * Destructor
 */
ScriptParserBase::~ScriptParserBase()
{

}

/**
 * Test if that name is already in use.
 */
bool ScriptParserBase::haveNameRef(const std::string& s) const
{
	auto ref = ScriptRef{ s.data(), s.data() + s.size() };
	if (findSortHelper(_refList, ref) != nullptr)
	{
		return true;
	}
	if (findSortHelper(_procList, ref) != nullptr)
	{
		return true;
	}
	if (findSortHelper(_typeList, ref) != nullptr)
	{
		return true;
	}
	for (auto& r : ConditionNames)
	{
		if (ref == r)
		{
			return true;
		}
	}
	for (auto& r : ConditionSpecNames)
	{
		if (ref == r)
		{
			return true;
		}
	}
	return false;
}

/**
 * Strore new name reference for future use.
 */
ScriptRef ScriptParserBase::addNameRef(const std::string& s)
{
	return addString(_strings, s);
}

/**
 * Add new function parsing arguments of script operation.
 * @param s function name
 * @param parser parsing fu
 */
void ScriptParserBase::addParserBase(const std::string& s, ScriptProcData::overloadFunc overload, ScriptRange<ScriptRange<ArgEnum>> overloadArg, ScriptProcData::parserFunc parser, ScriptProcData::argFunc arg, ScriptProcData::getFunc get)
{
	if (haveNameRef(s))
	{
		auto procs = getProc(ScriptRef{ s.data(), s.data() + s.size() });
		if (!procs)
		{
			throw Exception("Function name '" + s + "' already used");
		}
	}
	if (!parser)
	{
		parser = &parseCustomProc;
	}
	if (!overload)
	{
		overload = validOverloadProc(overloadArg) ? &overloadCustomProc : &overloadInvalidProc;
	}
	addSortHelper(_procList, { addNameRef(s), overload, overloadArg, parser, arg, get });
}

/**
 * Add new type to parser.
 * @param s Type name.
 * @param type
 * @param size
 */
void ScriptParserBase::addTypeBase(const std::string& s, ArgEnum type, size_t size)
{
	if (haveNameRef(s))
	{
		throw Exception("Type name '" + s + "' already used");
	}

	addSortHelper(_typeList, { addNameRef(s), ArgBase(type), size });
}

/**
 * Test if type is alredy used.
 */
bool ScriptParserBase::haveTypeBase(ArgEnum type)
{
	type = ArgBase(type);
	for (auto& v : _typeList)
	{
		if (v.type == type)
		{
			return true;
		}
	}
	return false;
}

/**
 * Add standard register.
 * @param s Name of register.
 * @param index Predefined position.
 */
void ScriptParserBase::addStandartReg(const std::string& s, RegEnum index)
{
	if (haveNameRef(s))
	{
		throw Exception("Reg name '" + s + "' already used");
	}

	addSortHelper(_refList, { addNameRef(s), ArgSpec(ArgInt, ArgSpecVar), index });
}

/**
 * Set name for custom script param.
 * @param s name for custom parameter.
 * @param type type of custom parameter.
 * @param size size of custom parameter.
 */
void ScriptParserBase::addScriptArg(const std::string& s, ArgEnum type)
{
	type = ArgSpec(ArgRemove(type, ArgSpecVar), ArgSpecReg);
	auto t = getType(type);
	if (t == nullptr)
	{
		throw Exception("Invalid type for reg: '" + s + "'");
	}
	auto size = getRegSize(*this, type);
	if (size == 0)
	{
		throw Exception("Invalid use of type '" + t->name.toString() + "' for reg: '" + s + "'");
	}
	if (_regUsed + size <= ScriptMaxReg)
	{
		if (haveNameRef(s))
		{
			throw Exception("Reg name '" + s + "' already used");
		}

		auto old = _regUsed;
		_regUsed += size;
		addSortHelper(_refList, { addNameRef(s), type, static_cast<RegEnum>(old) });
	}
	else
	{
		throw Exception("Custom arg limit reach for: '" + s + "'");
	}
}

/**
 * Add const value to script.
 * @param s name for const.
 * @param i value.
 */
void ScriptParserBase::addConst(const std::string& s, ScriptValueData i)
{
	if (haveNameRef(s))
	{
		throw Exception("Const name '" + s + "' already used");
	}

	addSortHelper(_refList, { addNameRef(s), i.type, i });
}

/**
 * Update const value in script.
 * @param s name for const.
 * @param i new value.
 */
void ScriptParserBase::updateConst(const std::string& s, ScriptValueData i)
{
	ScriptRefData* f = findSortHelper(_refList, ScriptRef{ s.data(), s.data() + s.size() });
	if (!f)
	{
		throw Exception("Unknown const with name '" + s + "' to update");
	}
	if (f->type != i.type)
	{
		throw Exception("Incompatible const with name '" + s + "' to update");
	}
	f->value = i;
}
/**
 * Get name of type
 * @param type Type id.
 */
ScriptRef ScriptParserBase::getTypeName(ArgEnum type) const
{
	auto p = getType(type);
	if (p)
	{
		return p->name;
	}
	else
	{
		return ScriptRef{ };
	}
}

/**
 * Get full name of type.
 * @param type
 * @return Full name with e.g. `var` or `ptr`.
 */
std::string ScriptParserBase::getTypePrefix(ArgEnum type) const
{
	std::string prefix;
	if (ArgIsVar(type))
	{
		prefix = "var ";
	}
	if (ArgIsPtr(type))
	{
		if (ArgIsPtrE(type))
		{
			prefix += "ptre ";
		}
		else
		{
			prefix += "ptr ";
		}
	}
	return prefix;
}

/**
 * Get type data.
 * @param type Type id.
 */
const ScriptTypeData* ScriptParserBase::getType(ArgEnum type) const
{
	ArgEnum base = ArgBase(type);
	for (auto& t : _typeList)
	{
		if (t.type == base)
		{
			return &t;
		}
	}
	return nullptr;
}

/**
 * Get type data with name equal prefix + postfix.
 * @param prefix Beginig of name.
 * @param postfix End of name.
 * @return Pointer to data or null if not find.
 */
const ScriptTypeData* ScriptParserBase::getType(ScriptRef prefix, ScriptRef postfix) const
{
	return findSortHelper(_typeList, prefix, postfix);
}

/**
 * Get function data with name equal prefix + postfix.
 * @param prefix Beginig of name.
 * @param postfix End of name.
 * @return Pointer to data or null if not find.
 */
ScriptRange<ScriptProcData> ScriptParserBase::getProc(ScriptRef prefix, ScriptRef postfix) const
{
	auto lower = _procList.data();
	auto upper = _procList.data() + _procList.size();
	lower = boundSortHelper<false>(lower, upper, prefix, postfix);
	upper = boundSortHelper<true>(lower, upper, prefix, postfix);

	return { lower, upper };
}

/**
 * Get arguments data with name equal prefix + postfix.
 * @param prefix Beginig of name.
 * @param postfix End of name.
 * @return Pointer to data or null if not find.
 */
const ScriptRefData* ScriptParserBase::getRef(ScriptRef prefix, ScriptRef postfix) const
{
	return findSortHelper(_refList, prefix, postfix);
}

/**
 * Parse string and write script to ScriptBase
 * @param src struct where final script is write to
 * @param src_code string with script
 * @return true if string have valid script
 */
bool ScriptParserBase::parseBase(ScriptContainerBase& destScript, const std::string& parentName, const std::string& srcCode) const
{
	ScriptContainerBase tempScript;
	std::string err = "Error in parsing script '" + _name + "' for '" + parentName + "': ";
	ParserWriter help(
		_regUsed,
		tempScript,
		*this
	);

	bool haveLastReturn = false;
	ScriptRef range = ScriptRef{ srcCode.data(), srcCode.data() + srcCode.size() };
	if (!range)
	{
		return false;
	}

	while (true)
	{
		SelectedToken op = range.getNextToken();
		if (!op)
		{
			for (auto i = help.refListCurr.begin(); i != help.refListCurr.end(); ++i)
			{
				if (i->type == ArgLabel && i->value.getValue<int>() == -1)
				{
					Log(LOG_ERROR) << err << "invalid use of label: '" << i->name.toString() << "' without declaration";
					return false;
				}
			}
			if (!haveLastReturn)
			{
				Log(LOG_ERROR) << err << "script need to end with return statemlent";
			}
			help.relese();
			destScript = std::move(tempScript);
			return true;
		}

		auto line_begin = op.begin();
		SelectedToken label = { };
		SelectedToken args[ScriptMaxArg];
		args[0] = range.getNextToken(TokenColon);
		if (args[0].getType() == TokenColon)
		{
			std::swap(op, label);
			op = range.getNextToken();
			args[0] = range.getNextToken();
		}

		// change form of "Reg.Function" to "Type.Function Reg".
		auto op_curr = getProc(op);
		if (!op_curr)
		{
			auto first_dot = op.find('.');
			if (first_dot != std::string::npos)
			{
				auto temp = op.substr(0, first_dot);
				auto ref = help.getReferece(temp);
				if (ref && ref.type >= ArgMax)
				{
					auto name = getTypeName(ref.type);
					if (name)
					{
						op_curr = getProc(name, op.substr(first_dot));
						if (!op_curr)
						{
							Log(LOG_ERROR) << err << "invalid operation name '"<< op.toString() <<"'";
							return false;
						}
						args[1] = args[0];
						args[0] = { TokenSymbol, temp };
					}
				}
			}
		}
		for (int i = (args[1] ? 2 : 1); i < ScriptMaxArg; ++i)
			args[i] = range.getNextToken();
		SelectedToken f = range.getNextToken(TokenSemicolon);

		//validation
		bool valid = true;
		valid &= label.getType() == TokenSymbol || label.getType() == TokenNone;
		valid &= op.getType() == TokenSymbol;
		for (int i = 0; i < ScriptMaxArg; ++i)
			valid &= args[i].getType() == TokenSymbol || args[i].getType() == TokenNumber || args[i].getType() == TokenNone;
		valid &= f.getType() == TokenSemicolon;

		if (!valid)
		{
			auto line_end = range.begin();
			if (f.getType() != TokenSemicolon)
			{
				// fixing `line_end` position.
				while(line_end != range.end() && *line_end != ';')
					++line_end;
				if (line_end != range.end())
					++line_end;
			}
			if (args[ScriptMaxArg - 1].getType() != TokenNone)
			{
				Log(LOG_ERROR) << err << "too many arguments in line: '" << std::string(line_begin, line_end) << "'";
			}
			else
			{
				Log(LOG_ERROR) << err << "invalid line: '" << std::string(line_begin, line_end) << "'";
			}
			return false;
		}
		else
		{
			ScriptRef line = ScriptRef{ line_begin, range.begin() };

			// matching args form operation definition with args avaliable in string
			int i = 0;
			while (i < ScriptMaxArg && args[i].getType() != TokenNone)
				++i;

			if (label && !help.setLabel(label, help.getCurrPos()))
			{
				Log(LOG_ERROR) << err << "invalid label '"<< label.toString() <<"' in line: '" << line.toString() << "'";
				return false;
			}

			haveLastReturn = op == ScriptRef{ "return" };

			// create normal proc call
			if (callOverloadProc(help, op_curr, args, args+i) == false)
			{
				Log(LOG_ERROR) << err << "invalid operation in line: '" << line.toString() << "'";
				return false;
			}
		}
	}
}

/**
 * Prase string and return new script.
 */
void ScriptParserBase::parseNode(ScriptContainerBase& container, const std::string& type, const YAML::Node& node) const
{
	if(const YAML::Node& scripts = node["scripts"])
	{
		if (const YAML::Node& curr = scripts[getName()])
		{
			parseBase(container, type, curr.as<std::string>());
		}
	}
	if (!container && !getDefault().empty())
	{
		parseBase(container, type, getDefault());
	}
}

/**
 * Load global data from YAML.
 */
void ScriptParserBase::load(const YAML::Node& node)
{

}

/**
 * Print all metadata
 */
void ScriptParserBase::logScriptMetadata(bool haveEvents) const
{
	if (Options::debug)
	{
		auto argType = [&](ArgEnum type) -> std::string
		{
			return getTypeName(type).toString();
		};

		const int tabSize = 8;
		static bool printOp = true;
		if (printOp)
		{
			size_t offset = 0;
			printOp = false;
			Logger opLog;
			#define MACRO_STRCAT(...) #__VA_ARGS__
			#define MACRO_ALL_LOG(NAME, Impl, Args) \
				if (validOverloadProc(helper::FuncGroup<MACRO_FUNC_ID(NAME)>::overloadType())) opLog.get(LOG_DEBUG) \
					<< "Op:    " << std::setw(tabSize*2) << #NAME \
					<< "OpId:  " << std::setw(tabSize/2) << offset << "  + " <<  std::setw(tabSize) << helper::FuncGroup<MACRO_FUNC_ID(NAME)>::ver() \
					<< "Impl:  " << std::setw(tabSize*10) << MACRO_STRCAT(Impl) \
					<< "Args:  " << displayOverloadProc(this, helper::FuncGroup<MACRO_FUNC_ID(NAME)>::overloadType()) << "\n"; \
				offset += helper::FuncGroup<MACRO_FUNC_ID(NAME)>::ver();

			opLog.get(LOG_DEBUG) << "Available buildin script operations:\n" << std::left << std::hex << std::showbase;
			MACRO_PROC_DEFINITION(MACRO_ALL_LOG)

			#undef MACRO_ALL_LOG
			#undef MACRO_STRCAT
		}

		Logger refLog;
		refLog.get(LOG_DEBUG) << "Script info for: " << _name << "\n" << std::left;
		refLog.get(LOG_DEBUG) << "\n";
		if (haveEvents)
		{
			refLog.get(LOG_DEBUG) << "Have global events\n";
			refLog.get(LOG_DEBUG) << "\n";
		}
		if (!_defaultScript.empty())
		{
			refLog.get(LOG_DEBUG) << "Script defualt implementation:\n";
			refLog.get(LOG_DEBUG) << _defaultScript << "\n";
			refLog.get(LOG_DEBUG) << "\n";
		}
		refLog.get(LOG_DEBUG) << "Script data:\n";
		auto temp = _refList;
		std::sort(temp.begin(), temp.end(),
			[](const ScriptRefData& a, const ScriptRefData& b)
			{
				return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end());
			}
		);
		for (auto& r : temp)
		{
			if (!ArgIsReg(r.type) && !ArgIsPtr(r.type) && Logger::reportingLevel() != LOG_VERBOSE)
			{
				continue;
			}
			if (ArgBase(r.type) == ArgInt && !ArgIsReg(r.type))
			{
				refLog.get(LOG_DEBUG) << "Name: " << std::setw(40) << r.name.toString() << std::setw(9) << getTypePrefix(r.type) << " " << std::setw(9) << argType(r.type) << " " << r.value.getValue<int>() << "\n";
			}
			else
			{
				refLog.get(LOG_DEBUG) << "Name: " << std::setw(40) << r.name.toString() << std::setw(9) << getTypePrefix(r.type) << " " << std::setw(9) << argType(r.type) << "\n";
			}
		}
		if (Logger::reportingLevel() != LOG_VERBOSE)
		{
			refLog.get(LOG_DEBUG) << "To see const values and custom operations use 'verboseLogging'\n";
		}
		else
		{
			auto temp = _procList;
			std::sort(temp.begin(), temp.end(),
				[](const ScriptProcData& a, const ScriptProcData& b)
				{
					return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end());
				}
			);

			refLog.get(LOG_DEBUG) << "\n";
			refLog.get(LOG_DEBUG) << "Script operations:\n";
			for (auto& p : temp)
			{
				if (p.parserArg != nullptr && p.overloadArg)
				{
					refLog.get(LOG_DEBUG) << "Name: " << std::setw(40) << p.name.toString() << "Args: " << displayOverloadProc(this, p.overloadArg) << "\n";
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////
//				ScriptParserEventsBase class
////////////////////////////////////////////////////////////

/**
 * Constructor.
 */
ScriptParserEventsBase::ScriptParserEventsBase(ScriptGlobal* shared, const std::string& name, const std::string& firstArg, const std::string& secondArg) : ScriptParserBase(shared, name, firstArg, secondArg)
{
	_events.reserve(EventsMax);
	_eventsData.push_back({ 0, {} });
	_eventsData.push_back({ OffsetMax, {} });
}

/**
 *  Prase string and return new script.
 */
void ScriptParserEventsBase::parseNode(ScriptContainerEventsBase& container, const std::string& type, const YAML::Node& node) const
{
	ScriptParserBase::parseNode(container._current, type, node);
	container._events = getEvents();
}

/**
 * Load global data from YAML.
 */
void ScriptParserEventsBase::load(const YAML::Node& node)
{
	ScriptParserBase::load(node);
	if(const YAML::Node& scripts = node["scripts"])
	{
		if (const YAML::Node& curr = scripts[getName()])
		{
			for (const YAML::Node& i : curr)
			{
				EventData data = EventData{};
				data.offset = i["offset"].as<double>(0) * OffsetScale;
				if (data.offset == 0 || data.offset >= (int)OffsetMax || data.offset <= -(int)OffsetMax)
				{
					Log(LOG_ERROR) << "Invalid offset for '" << getName() << "' equal: '" << i["offset"].as<std::string>() << "'";
					continue;
				}
				ScriptContainerBase scp;
				if (parseBase(scp, "Global Event Script", i["code"].as<std::string>("")))
				{
					data.script = std::move(scp);
					_eventsData.push_back(std::move(data));
				}
			}
		}
	}
}

/**
 * Get pointer to events.
 */
const ScriptContainerBase* ScriptParserEventsBase::getEvents() const
{
	return _events.data();
}

/**
 * Relese event data.
 */
std::vector<ScriptContainerBase> ScriptParserEventsBase::releseEvents()
{
	std::sort(std::begin(_eventsData), std::end(_eventsData), [](const EventData& a, const EventData& b) { return a.offset < b.offset; });
	for (auto& e : _eventsData)
	{
		if (_events.size() < EventsMax)
		{
			_events.push_back(std::move(e.script));
		}
	}
	return std::move(_events);
}

////////////////////////////////////////////////////////////
//					ScriptValuesBase class
////////////////////////////////////////////////////////////

/**
 * Set value.
 */
void ScriptValuesBase::setBase(size_t t, int i)
{
	if (t)
	{
		if (t > values.size())
		{
			values.resize(t);
		}
		values[t - 1u] = i;
	}
}

/**
 * Get value.
 */
int ScriptValuesBase::getBase(size_t t) const
{
	if (t && t <= values.size())
	{
		return values[t - 1u];
	}
	return 0;
}

/**
 * Load values from yaml file.
 */
void ScriptValuesBase::loadBase(const YAML::Node &node, const ScriptGlobal* shared, ArgEnum type)
{
	if (const YAML::Node& tags = node["tags"])
	{
		if (tags.IsMap())
		{
			for (const std::pair<YAML::Node, YAML::Node>& pair : tags)
			{
				size_t i = shared->getTag(type, ScriptRef::tempFrom("Tag." + pair.first.as<std::string>()));
				if (i)
				{
					auto temp = 0;
					auto data = shared->getTagValueData(type, i);
					shared->getTagValueType(data.type).load(shared, temp, pair.second);
					setBase(i, temp);
				}
			}
		}
	}
}

/**
 * Save values to yaml file.
 */
void ScriptValuesBase::saveBase(YAML::Node &node, const ScriptGlobal* shared, ArgEnum type) const
{
	YAML::Node tags;
	for (size_t i = 1; i <= values.size(); ++i)
	{
		if (int v = getBase(i))
		{
			auto temp = YAML::Node{};
			auto data = shared->getTagValueData(type, i);
			shared->getTagValueType(data.type).save(shared, v, temp);
			tags[data.name.substr(data.name.find('.') + 1u).toString()] = temp;
		}
	}
	node["tags"] = tags;
}

////////////////////////////////////////////////////////////
//					ScriptGlobal class
////////////////////////////////////////////////////////////

/**
 * Default constructor.
 */
ScriptGlobal::ScriptGlobal()
{
	addTagValueTypeBase(
		"int",
		[](const ScriptGlobal* s, int& value, const YAML::Node& node)
		{
			if (node)
			{
				value = node.as<int>();
			}
		},
		[](const ScriptGlobal* s, const int& value, YAML::Node& node)
		{
			node = value;
		}
	);
}

/**
 * Destructor.
 */
ScriptGlobal::~ScriptGlobal()
{

}

/**
 * Get tag value.
 */
size_t ScriptGlobal::getTag(ArgEnum type, ScriptRef s) const
{
	auto data = _tagNames.find(type);
	if (data != _tagNames.end())
	{
		for (size_t i = 0; i < data->second.values.size(); ++i)
		{
			auto &name = data->second.values[i].name;
			if (name == s)
			{
				return i + 1;
			}
		}
	}
	return 0;
}

/**
 * Get name of tag value.
 */
ScriptGlobal::TagValueData ScriptGlobal::getTagValueData(ArgEnum type, size_t i) const
{
	auto data = _tagNames.find(type);
	if (data != _tagNames.end())
	{
		if (i && i <= data->second.values.size())
		{
			return data->second.values[i - 1];
		}
	}
	return {};
}

/**
 * Get tag value type.
 */
ScriptGlobal::TagValueType ScriptGlobal::getTagValueType(size_t valueType) const
{
	if (valueType < _tagValueTypes.size())
	{
		return _tagValueTypes[valueType];
	}
	return {};
}

/**
 * Add new tag name.
 */
size_t ScriptGlobal::addTag(ArgEnum type, ScriptRef s, size_t valueType)
{
	auto& data = _tagNames[type];
	auto tag = getTag(type, s);

	if (tag == 0)
	{
		// test to prevent warp of index value
		if (data.values.size() < data.limit)
		{
			data.values.push_back(TagValueData{ s, valueType });
			return data.values.size();
		}
		return 0;
	}
	else
	{
		return tag;
	}
}

/**
 * Strore new name reference for future use.
 */
ScriptRef ScriptGlobal::addNameRef(const std::string& s)
{
	return addString(_strings, s);
}

/**
 * Store parser.
 */
void ScriptGlobal::pushParser(ScriptParserBase* parser)
{
	parser->logScriptMetadata(false);
	_parserNames.insert(std::make_pair(parser->getName(), parser));
}

/**
 * Store parser with events.
 */
void ScriptGlobal::pushParser(ScriptParserEventsBase* parser)
{
	parser->logScriptMetadata(true);
	_parserNames.insert(std::make_pair(parser->getName(), parser));
	_parserEvents.push_back(parser);
}

/**
 * Add new const value.
 */
void ScriptGlobal::addConst(const std::string& name, ScriptValueData i)
{
	for (auto p : _parserNames)
	{
		p.second->addConst(name, i);
	}
}

/**
 * Update const value.
 */
void ScriptGlobal::updateConst(const std::string& name, ScriptValueData i)
{
	for (auto p : _parserNames)
	{
		p.second->updateConst(name, i);
	}
}

/**
 * Prepare for loading data.
 */
void ScriptGlobal::beginLoad()
{

}

/**
 * Finishing loading data.
 */
void ScriptGlobal::endLoad()
{
	for (auto& p : _parserEvents)
	{
		_events.push_back(p->releseEvents());
	}
	_parserNames.clear();
	_parserEvents.clear();
}

/**
 * Load global data from YAML.
 */
void ScriptGlobal::load(const YAML::Node& node)
{
	if (const YAML::Node& t = node["tags"])
	{
		for (auto& p : _tagNames)
		{
			const auto nodeName = p.second.name.toString();
			const YAML::Node &tags = t[nodeName];
			if (tags && tags.IsMap())
			{
				for (const std::pair<YAML::Node, YAML::Node>& i : tags)
				{
					auto type = i.second.as<std::string>();
					auto name = i.first.as<std::string>();
					auto invalidType = _tagValueTypes.size();
					auto valueType = invalidType;
					for (size_t t = 0; t < invalidType; ++t)
					{
						if (ScriptRef::tempFrom(type) == _tagValueTypes[t].name)
						{
							valueType = t;
							break;
						}
					}
					if (valueType != invalidType)
					{
						auto namePrefix = "Tag." + name;
						auto tag = getTag(p.first, ScriptRef::tempFrom(namePrefix));
						if (tag)
						{
							auto data = getTagValueData(p.first, tag);
							if (valueType != data.type)
							{
								Log(LOG_ERROR) << "Script variable '" + name + "' have wrong type '" << _tagValueTypes[valueType].name.toString() << "' instead of '" << _tagValueTypes[data.type].name.toString() << "' in '" + nodeName + "'.";
							}
							continue;
						}
						tag = addTag(p.first, addNameRef(namePrefix), valueType);
						if (!tag)
						{
							Log(LOG_ERROR) << "Script variable '" + name + "' exceeds limit of " << (int)p.second.limit << " avaiable variables in '" + nodeName + "'.";
							continue;
						}
					}
					else
					{
						Log(LOG_ERROR) << "Invaild type def '" + type + "' for script variable '" + name + "' in '" + nodeName +"'.";
					}
				}
			}
		}
	}
	for (auto& p : _parserNames)
	{
		p.second->load(node);
	}
}

} //namespace OpenXcom
