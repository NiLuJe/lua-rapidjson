#ifndef __LUA_RAPIDJSON_CALIBRE_HPP__
#define __LUA_RAPIDJSON_CALIBRE_HPP__

#include <string>
#include <unordered_set>

#include <lua.hpp>
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/reader.h>
#include <rapidjson/error/en.h>

#include "luax.hpp"
#include "StringStream.hpp"
#include "values.hpp"

namespace calibre {
	/**
	 * Handle json SAX events and create relevant Lua objects.
	 */
	struct ToLuaHandler {
		explicit ToLuaHandler(lua_State* aL) : required_field(false), depth(0), L(aL) { stack_.reserve(32); }

		bool Null() {
			if (!required_field) {
				return true;
			}

			values::push_null(L);
			context_.submit(L);
			return true;
		}
		bool Bool(bool b) {
			if (!required_field) {
				return true;
			}

			lua_pushboolean(L, b);
			context_.submit(L);
			return true;
		}
		bool Int(int i) {
			if (!required_field) {
				return true;
			}

			lua_pushinteger(L, i);
			context_.submit(L);
			return true;
		}
		bool Uint(unsigned u) {
			if (!required_field) {
				return true;
			}

			if (sizeof(lua_Integer) > sizeof(unsigned int) || u <= static_cast<unsigned>(std::numeric_limits<lua_Integer>::max())) {
				lua_pushinteger(L, static_cast<lua_Integer>(u));
			} else {
				lua_pushnumber(L, static_cast<lua_Number>(u));
			}
			context_.submit(L);
			return true;
		}
		bool Int64(int64_t i) {
			if (!required_field) {
				return true;
			}

			if (sizeof(lua_Integer) >= sizeof(int64_t) || (i <= std::numeric_limits<lua_Integer>::max() && i >= std::numeric_limits<lua_Integer>::min())) {
				lua_pushinteger(L, static_cast<lua_Integer>(i));
			} else {
				lua_pushnumber(L, static_cast<lua_Number>(i));
			}
			context_.submit(L);
			return true;
		}
		bool Uint64(uint64_t u) {
			if (!required_field) {
				return true;
			}

			if (sizeof(lua_Integer) > sizeof(uint64_t) || u <= static_cast<uint64_t>(std::numeric_limits<lua_Integer>::max()))
				lua_pushinteger(L, static_cast<lua_Integer>(u));
			else
				lua_pushnumber(L, static_cast<lua_Number>(u));
			context_.submit(L);
			return true;
		}
		bool Double(double d) {
			if (!required_field) {
				return true;
			}

			lua_pushnumber(L, static_cast<lua_Number>(d));
			context_.submit(L);
			return true;
		}
		bool RawNumber(const char* str, rapidjson::SizeType length, bool copy) {
			if (!required_field) {
				return true;
			}

			lua_getglobal(L, "tonumber");
			lua_pushlstring(L, str, length);
			lua_call(L, 1, 1);
			context_.submit(L);
			return true;
		}
		bool String(const char* str, rapidjson::SizeType length, bool copy) {
			if (!required_field) {
				return true;
			}

			lua_pushlstring(L, str, length);
			context_.submit(L);
			return true;
		}
		bool StartObject() {
			// We only want the book object @ depth 0 (pre-increment), not any of the nested ones.
			if (depth++ > 0) {
				return true;
			}

			// We know exactly how many key-value pairs we'll need
			lua_createtable(L, 0, required_fields.size());

			// Switch to the object (i.e., hash) context
			stack_.push_back(context_);
			context_ = Ctx::Object();
			return true;
		}
		bool Key(const char* str, rapidjson::SizeType length, bool copy) const {
			// We only care about a few specific fields
			// NOTE: contains is C++20 ;'(.
			//       But this is a set, so this can only ever return 0 or 1.
			if (required_fields.count(str)) {
				lua_pushlstring(L, str, length);
				required_field = true;
			} else {
				required_field = false;
			}
			return true;
		}
		bool EndObject(rapidjson::SizeType memberCount) {
			// We only create the book-level object
			if (--depth > 0) {
				return true;
			}

			// Switch back to the previous context
			context_ = stack_.back();
			stack_.pop_back();
			context_.submit(L);
			return true;
		}
		bool StartArray() {
			// We *also* want the top-level array, when we don't actually have field info yet ;).
			if (!required_field && depth > 0) {
				return true;
			}

			lua_newtable(L);

			// Switch to the array context
			stack_.push_back(context_);
			context_ = Ctx::Array();
			return true;
		}
		bool EndArray(rapidjson::SizeType elementCount) {
			// Same logic as in StartArray
			if (!required_field && depth > 0) {
				return true;
			}

			// Switch back to the previous context
			assert(elementCount == context_.index_);
			context_ = stack_.back();
			stack_.pop_back();
			context_.submit(L);
			return true;
		}

	private:
		struct Ctx {
			Ctx() : index_(0), fn_(&topFn) {}
			Ctx(const Ctx& rhs) : index_(rhs.index_), fn_(rhs.fn_)
			{
			}
			const Ctx& operator=(const Ctx& rhs) {
				if (this != &rhs) {
					index_ = rhs.index_;
					fn_ = rhs.fn_;
				}
				return *this;
			}
			static Ctx Object() {
				return Ctx(&objectFn);
			}
			static Ctx Array()
			{
				return Ctx(&arrayFn);
			}
			void submit(lua_State* L)
			{
				fn_(L, this);
			}

			int index_;
			void(*fn_)(lua_State* L, Ctx* ctx);

		private:
			explicit Ctx(void(*f)(lua_State* L, Ctx* ctx)) : index_(0), fn_(f) {}


			static void objectFn(lua_State* L, Ctx* ctx)
			{
				lua_rawset(L, -3);	// t[k] = v (i.e., hash)
														// t@-3[-2] = -1 (i.e., v @ the top, k below it; both get pop'ed)
			}

			static void arrayFn(lua_State* L, Ctx* ctx)
			{
				lua_rawseti(L, -2, ++ctx->index_);	// t[n] = v (i.e., array)
																						// t@-2[index] = -1 (i.e., v @ the top, gets pop'ed)
			}
			static void topFn(lua_State* L, Ctx* ctx)
			{
			}
		};

		mutable bool required_field;
		size_t depth;
		const std::unordered_set<std::string> required_fields {
			"authors",
			"last_modified",
			"lpath",
			"series",
			"series_index",
			"size",
			"tags",
			"title",
			"uuid ",
		};

		lua_State* L;
		std::vector <Ctx> stack_;
		Ctx context_;
	};

	template<typename Stream>
	inline int pushDecoded(lua_State* L, Stream& s) {
		int top = lua_gettop(L);
		calibre::ToLuaHandler handler(L);
		rapidjson::Reader reader;
		rapidjson::ParseResult r = reader.Parse(s, handler);

		if (!r) {
			lua_settop(L, top);
			lua_pushnil(L);
			lua_pushfstring(L, "%s (%d)", rapidjson::GetParseError_En(r.Code()), r.Offset());
			return 2;
		}

		return 1;
	}
}

#endif // __LUA_RAPIDJSON_TOLUAHANDLER_HPP__
