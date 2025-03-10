#ifndef JWT_CPP_JWT_H
#define JWT_CPP_JWT_H

#ifndef JWT_DISABLE_PICOJSON
#ifndef PICOJSON_USE_INT64
#define PICOJSON_USE_INT64
#endif
#include "picojson/picojson.h"
#endif

#ifndef JWT_DISABLE_BASE64
#include "base.h"
#endif

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <functional>
#include <iterator>
#include <locale>
#include <memory>
#include <set>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "error.h"
#include "crypto.h"

#if __cplusplus >= 201402L
#ifdef __has_include
#if __has_include(<experimental/type_traits>)
#include <experimental/type_traits>
#endif
#endif
#endif

#ifndef JWT_CLAIM_EXPLICIT
#define JWT_CLAIM_EXPLICIT explicit
#endif

/**
 * \brief JSON Web Token
 *
 * A namespace to contain everything related to handling JSON Web Tokens, JWT for short,
 * as a part of [RFC7519](https://tools.ietf.org/html/rfc7519), or alternatively for
 * JWS (JSON Web Signature) from [RFC7515](https://tools.ietf.org/html/rfc7515)
 */
namespace jwt {
	using date = std::chrono::system_clock::time_point;

	/**
	 * \brief JSON Abstractions for working with any library
	 */
	namespace json {
		/**
		 * \brief Generic JSON types used in JWTs
		 *
		 * This enum is to abstract the third party underlying types
		 */
		enum class type { boolean, integer, number, string, array, object };
	} // namespace json

	namespace details {
#ifdef __cpp_lib_void_t
		template<typename... Ts>
		using void_t = std::void_t<Ts...>;
#else
		// https://en.cppreference.com/w/cpp/types/void_t
		template<typename... Ts>
		struct make_void {
			using type = void;
		};

		template<typename... Ts>
		using void_t = typename make_void<Ts...>::type;
#endif

#ifdef __cpp_lib_experimental_detect
		template<template<typename...> class _Op, typename... _Args>
		using is_detected = std::experimental::is_detected<_Op, _Args...>;

		template<template<typename...> class _Op, typename... _Args>
		using is_detected_t = std::experimental::detected_t<_Op, _Args...>;
#else
		struct nonesuch {
			nonesuch() = delete;
			~nonesuch() = delete;
			nonesuch(nonesuch const&) = delete;
			nonesuch(nonesuch const&&) = delete;
			void operator=(nonesuch const&) = delete;
			void operator=(nonesuch&&) = delete;
		};

		// https://en.cppreference.com/w/cpp/experimental/is_detected
		template<class Default, class AlwaysVoid, template<class...> class Op, class... Args>
		struct detector {
			using value = std::false_type;
			using type = Default;
		};

		template<class Default, template<class...> class Op, class... Args>
		struct detector<Default, void_t<Op<Args...>>, Op, Args...> {
			using value = std::true_type;
			using type = Op<Args...>;
		};

		template<template<class...> class Op, class... Args>
		using is_detected = typename detector<nonesuch, void, Op, Args...>::value;

		template<template<class...> class Op, class... Args>
		using is_detected_t = typename detector<nonesuch, void, Op, Args...>::type;
#endif

		template<typename traits_type>
		using get_type_function = decltype(traits_type::get_type);

		template<typename traits_type, typename value_type>
		using is_get_type_signature =
			typename std::is_same<get_type_function<traits_type>, json::type(const value_type&)>;

		template<typename traits_type, typename value_type>
		struct supports_get_type {
			static constexpr auto value = is_detected<get_type_function, traits_type>::value &&
										  std::is_function<get_type_function<traits_type>>::value &&
										  is_get_type_signature<traits_type, value_type>::value;
		};

		template<typename traits_type>
		using as_object_function = decltype(traits_type::as_object);

		template<typename traits_type, typename value_type, typename object_type>
		using is_as_object_signature =
			typename std::is_same<as_object_function<traits_type>, object_type(const value_type&)>;

		template<typename traits_type, typename value_type, typename object_type>
		struct supports_as_object {
			static constexpr auto value = std::is_constructible<value_type, object_type>::value &&
										  is_detected<as_object_function, traits_type>::value &&
										  std::is_function<as_object_function<traits_type>>::value &&
										  is_as_object_signature<traits_type, value_type, object_type>::value;
		};

		template<typename traits_type>
		using as_array_function = decltype(traits_type::as_array);

		template<typename traits_type, typename value_type, typename array_type>
		using is_as_array_signature =
			typename std::is_same<as_array_function<traits_type>, array_type(const value_type&)>;

		template<typename traits_type, typename value_type, typename array_type>
		struct supports_as_array {
			static constexpr auto value = std::is_constructible<value_type, array_type>::value &&
										  is_detected<as_array_function, traits_type>::value &&
										  std::is_function<as_array_function<traits_type>>::value &&
										  is_as_array_signature<traits_type, value_type, array_type>::value;
		};

		template<typename traits_type>
		using as_string_function = decltype(traits_type::as_string);

		template<typename traits_type, typename value_type, typename string_type>
		using is_as_string_signature =
			typename std::is_same<as_string_function<traits_type>, string_type(const value_type&)>;

		template<typename traits_type, typename value_type, typename string_type>
		struct supports_as_string {
			static constexpr auto value = std::is_constructible<value_type, string_type>::value &&
										  is_detected<as_string_function, traits_type>::value &&
										  std::is_function<as_string_function<traits_type>>::value &&
										  is_as_string_signature<traits_type, value_type, string_type>::value;
		};

		template<typename traits_type>
		using as_number_function = decltype(traits_type::as_number);

		template<typename traits_type, typename value_type, typename number_type>
		using is_as_number_signature =
			typename std::is_same<as_number_function<traits_type>, number_type(const value_type&)>;

		template<typename traits_type, typename value_type, typename number_type>
		struct supports_as_number {
			static constexpr auto value = std::is_floating_point<number_type>::value &&
										  std::is_constructible<value_type, number_type>::value &&
										  is_detected<as_number_function, traits_type>::value &&
										  std::is_function<as_number_function<traits_type>>::value &&
										  is_as_number_signature<traits_type, value_type, number_type>::value;
		};

		template<typename traits_type>
		using as_integer_function = decltype(traits_type::as_int);

		template<typename traits_type, typename value_type, typename integer_type>
		using is_as_integer_signature =
			typename std::is_same<as_integer_function<traits_type>, integer_type(const value_type&)>;

		template<typename traits_type, typename value_type, typename integer_type>
		struct supports_as_integer {
			static constexpr auto value = std::is_signed<integer_type>::value &&
										  !std::is_floating_point<integer_type>::value &&
										  std::is_constructible<value_type, integer_type>::value &&
										  is_detected<as_integer_function, traits_type>::value &&
										  std::is_function<as_integer_function<traits_type>>::value &&
										  is_as_integer_signature<traits_type, value_type, integer_type>::value;
		};

		template<typename traits_type>
		using as_boolean_function = decltype(traits_type::as_bool);

		template<typename traits_type, typename value_type, typename boolean_type>
		using is_as_boolean_signature =
			typename std::is_same<as_boolean_function<traits_type>, boolean_type(const value_type&)>;

		template<typename traits_type, typename value_type, typename boolean_type>
		struct supports_as_boolean {
			static constexpr auto value = std::is_convertible<boolean_type, bool>::value &&
										  std::is_constructible<value_type, boolean_type>::value &&
										  is_detected<as_boolean_function, traits_type>::value &&
										  std::is_function<as_boolean_function<traits_type>>::value &&
										  is_as_boolean_signature<traits_type, value_type, boolean_type>::value;
		};

		template<typename traits>
		struct is_valid_traits {
			// Internal assertions for better feedback
			static_assert(supports_get_type<traits, typename traits::value_type>::value,
						  "traits must provide `jwt::json::type get_type(const value_type&)`");
			static_assert(supports_as_object<traits, typename traits::value_type, typename traits::object_type>::value,
						  "traits must provide `object_type as_object(const value_type&)`");
			static_assert(supports_as_array<traits, typename traits::value_type, typename traits::array_type>::value,
						  "traits must provide `array_type as_array(const value_type&)`");
			static_assert(supports_as_string<traits, typename traits::value_type, typename traits::string_type>::value,
						  "traits must provide `string_type as_string(const value_type&)`");
			static_assert(supports_as_number<traits, typename traits::value_type, typename traits::number_type>::value,
						  "traits must provide `number_type as_number(const value_type&)`");
			static_assert(
				supports_as_integer<traits, typename traits::value_type, typename traits::integer_type>::value,
				"traits must provide `integer_type as_int(const value_type&)`");
			static_assert(
				supports_as_boolean<traits, typename traits::value_type, typename traits::boolean_type>::value,
				"traits must provide `boolean_type as_bool(const value_type&)`");

			static constexpr auto value =
				supports_get_type<traits, typename traits::value_type>::value &&
				supports_as_object<traits, typename traits::value_type, typename traits::object_type>::value &&
				supports_as_array<traits, typename traits::value_type, typename traits::array_type>::value &&
				supports_as_string<traits, typename traits::value_type, typename traits::string_type>::value &&
				supports_as_number<traits, typename traits::value_type, typename traits::number_type>::value &&
				supports_as_integer<traits, typename traits::value_type, typename traits::integer_type>::value &&
				supports_as_boolean<traits, typename traits::value_type, typename traits::boolean_type>::value;
		};

		template<typename value_type>
		struct is_valid_json_value {
			static constexpr auto value =
				std::is_default_constructible<value_type>::value &&
				std::is_constructible<value_type, const value_type&>::value && // a more generic is_copy_constructible
				std::is_move_constructible<value_type>::value && std::is_assignable<value_type, value_type>::value &&
				std::is_copy_assignable<value_type>::value && std::is_move_assignable<value_type>::value;
			// TODO(cmcarthur): Stream operators
		};

		template<typename traits_type>
		using has_mapped_type = typename traits_type::mapped_type;

		template<typename traits_type>
		using has_key_type = typename traits_type::key_type;

		template<typename traits_type>
		using has_value_type = typename traits_type::value_type;

		template<typename object_type>
		using has_iterator = typename object_type::iterator;

		template<typename object_type>
		using has_const_iterator = typename object_type::const_iterator;

		template<typename object_type>
		using is_begin_signature =
			typename std::is_same<decltype(std::declval<object_type>().begin()), has_iterator<object_type>>;

		template<typename object_type>
		using is_begin_const_signature =
			typename std::is_same<decltype(std::declval<const object_type>().begin()), has_const_iterator<object_type>>;

		template<typename object_type>
		struct supports_begin {
			static constexpr auto value =
				is_detected<has_iterator, object_type>::value && is_detected<has_const_iterator, object_type>::value &&
				is_begin_signature<object_type>::value && is_begin_const_signature<object_type>::value;
		};

		template<typename object_type>
		using is_end_signature =
			typename std::is_same<decltype(std::declval<object_type>().end()), has_iterator<object_type>>;

		template<typename object_type>
		using is_end_const_signature =
			typename std::is_same<decltype(std::declval<const object_type>().end()), has_const_iterator<object_type>>;

		template<typename object_type>
		struct supports_end {
			static constexpr auto value =
				is_detected<has_iterator, object_type>::value && is_detected<has_const_iterator, object_type>::value &&
				is_end_signature<object_type>::value && is_end_const_signature<object_type>::value;
		};

		template<typename object_type, typename string_type>
		using is_count_signature = typename std::is_integral<decltype(std::declval<const object_type>().count(
			std::declval<const string_type>()))>;

		template<typename object_type, typename value_type, typename string_type>
		using is_subcription_operator_signature =
			typename std::is_same<decltype(std::declval<object_type>()[std::declval<const string_type>()]),
								  value_type&>;

		template<typename object_type, typename value_type, typename string_type>
		using is_at_const_signature =
			typename std::is_same<decltype(std::declval<const object_type>().at(std::declval<const string_type>())),
								  const value_type&>;

		template<typename value_type, typename string_type, typename object_type>
		struct is_valid_json_object {
			static constexpr auto value =
				is_detected<has_mapped_type, object_type>::value &&
				std::is_same<typename object_type::mapped_type, value_type>::value &&
				is_detected<has_key_type, object_type>::value &&
				std::is_same<typename object_type::key_type, string_type>::value &&
				supports_begin<object_type>::value && supports_end<object_type>::value &&
				is_count_signature<object_type, string_type>::value &&
				is_subcription_operator_signature<object_type, value_type, string_type>::value &&
				is_at_const_signature<object_type, value_type, string_type>::value;

			static constexpr auto supports_claims_transform =
				value && is_detected<has_value_type, object_type>::value &&
				std::is_same<typename object_type::value_type, std::pair<const string_type, value_type>>::value;
		};

		template<typename value_type, typename array_type>
		struct is_valid_json_array {
			static constexpr auto value = std::is_same<typename array_type::value_type, value_type>::value;
		};

		template<typename value_type, typename string_type, typename object_type, typename array_type>
		struct is_valid_json_types {
			// Internal assertions for better feedback
			static_assert(is_valid_json_value<value_type>::value,
						  "value type must meet basic requirements, default constructor, copyable, moveable");
			static_assert(is_valid_json_object<value_type, string_type, object_type>::value,
						  "object_type must be a string_type to value_type container");
			static_assert(is_valid_json_array<value_type, array_type>::value,
						  "array_type must be a container of value_type");

			static constexpr auto value = is_valid_json_object<value_type, string_type, object_type>::value &&
										  is_valid_json_value<value_type>::value &&
										  is_valid_json_array<value_type, array_type>::value;
		};
	} // namespace details

	/**
	 * \brief a class to store a generic JSON value as claim
	 *
	 * The default template parameters use [picojson](https://github.com/kazuho/picojson)
	 *
	 * \tparam json_traits : JSON implementation traits
	 *
	 * \see [RFC 7519: JSON Web Token (JWT)](https://tools.ietf.org/html/rfc7519)
	 */
	template<typename json_traits>
	class basic_claim {
		/**
		 * The reason behind this is to provide an expressive abstraction without
		 * over complexifying the API. For more information take the time to read
		 * https://github.com/nlohmann/json/issues/774. It maybe be expanded to
		 * support custom string types.
		 */
		static_assert(std::is_same<typename json_traits::string_type, std::string>::value,
					  "string_type must be a std::string.");

		static_assert(
			details::is_valid_json_types<typename json_traits::value_type, typename json_traits::string_type,
										 typename json_traits::object_type, typename json_traits::array_type>::value,
			"must staisfy json container requirements");
		static_assert(details::is_valid_traits<json_traits>::value, "traits must satisfy requirements");

		typename json_traits::value_type val;

	public:
		using set_t = std::set<typename json_traits::string_type>;

		basic_claim() = default;
		basic_claim(const basic_claim&) = default;
		basic_claim(basic_claim&&) = default;
		basic_claim& operator=(const basic_claim&) = default;
		basic_claim& operator=(basic_claim&&) = default;
		~basic_claim() = default;

		JWT_CLAIM_EXPLICIT basic_claim(typename json_traits::string_type s) : val(std::move(s)) {}
		JWT_CLAIM_EXPLICIT basic_claim(const date& d)
			: val(typename json_traits::integer_type(std::chrono::system_clock::to_time_t(d))) {}
		JWT_CLAIM_EXPLICIT basic_claim(typename json_traits::array_type a) : val(std::move(a)) {}
		JWT_CLAIM_EXPLICIT basic_claim(typename json_traits::value_type v) : val(std::move(v)) {}
		JWT_CLAIM_EXPLICIT basic_claim(const set_t& s) : val(typename json_traits::array_type(s.begin(), s.end())) {}
		template<typename Iterator>
		basic_claim(Iterator begin, Iterator end) : val(typename json_traits::array_type(begin, end)) {}

		/**
		 * Get wrapped JSON value
		 * \return Wrapped JSON value
		 */
		typename json_traits::value_type to_json() const { return val; }

		/**
		 * Parse input stream into underlying JSON value
		 * \return input stream
		 */
		std::istream& operator>>(std::istream& is) { return is >> val; }

		/**
		 * Serialize claim to output stream from wrapped JSON value
		 * \return ouput stream
		 */
		std::ostream& operator<<(std::ostream& os) { return os << val; }

		/**
		 * Get type of contained JSON value
		 * \return Type
		 * \throw std::logic_error An internal error occured
		 */
		json::type get_type() const { return json_traits::get_type(val); }

		/**
		 * Get the contained JSON value as a string
		 * \return content as string
		 * \throw std::bad_cast Content was not a string
		 */
		typename json_traits::string_type as_string() const { return json_traits::as_string(val); }

		/**
		 * Get the contained JSON value as a date
		 * \return content as date
		 * \throw std::bad_cast Content was not a date
		 */
		date as_date() const { return std::chrono::system_clock::from_time_t(as_int()); }

		/**
		 * Get the contained JSON value as an array
		 * \return content as array
		 * \throw std::bad_cast Content was not an array
		 */
		typename json_traits::array_type as_array() const { return json_traits::as_array(val); }

		/**
		 * Get the contained JSON value as a set of strings
		 * \return content as set of strings
		 * \throw std::bad_cast Content was not an array of string
		 */
		set_t as_set() const {
			set_t res;
			for (const auto& e : json_traits::as_array(val)) {
				res.insert(json_traits::as_string(e));
			}
			return res;
		}

		/**
		 * Get the contained JSON value as an integer
		 * \return content as int
		 * \throw std::bad_cast Content was not an int
		 */
		typename json_traits::integer_type as_int() const { return json_traits::as_int(val); }

		/**
		 * Get the contained JSON value as a bool
		 * \return content as bool
		 * \throw std::bad_cast Content was not a bool
		 */
		typename json_traits::boolean_type as_bool() const { return json_traits::as_bool(val); }

		/**
		 * Get the contained JSON value as a number
		 * \return content as double
		 * \throw std::bad_cast Content was not a number
		 */
		typename json_traits::number_type as_number() const { return json_traits::as_number(val); }
	};

	namespace error {
		struct invalid_json_exception : public std::runtime_error {
			invalid_json_exception() : runtime_error("invalid json") {}
		};
		struct claim_not_present_exception : public std::out_of_range {
			claim_not_present_exception() : out_of_range("claim not found") {}
		};
	} // namespace error

	namespace details {
		template<typename json_traits>
		class map_of_claims {
			typename json_traits::object_type claims;

		public:
			using basic_claim_t = basic_claim<json_traits>;
			using iterator = typename json_traits::object_type::iterator;
			using const_iterator = typename json_traits::object_type::const_iterator;

			map_of_claims() = default;
			map_of_claims(const map_of_claims&) = default;
			map_of_claims(map_of_claims&&) = default;
			map_of_claims& operator=(const map_of_claims&) = default;
			map_of_claims& operator=(map_of_claims&&) = default;

			map_of_claims(typename json_traits::object_type json) : claims(std::move(json)) {}

			iterator begin() { return claims.begin(); }
			iterator end() { return claims.end(); }
			const_iterator cbegin() const { return claims.begin(); }
			const_iterator cend() const { return claims.end(); }
			const_iterator begin() const { return claims.begin(); }
			const_iterator end() const { return claims.end(); }

			/**
			 * \brief Parse a JSON string into a map of claims
			 *
			 * The implication is that a "map of claims" is identic to a JSON object
			 *
			 * \param str JSON data to be parse as an object
			 * \return content as JSON object
			 */
			static typename json_traits::object_type parse_claims(const typename json_traits::string_type& str) {
				typename json_traits::value_type val;
				if (!json_traits::parse(val, str)) throw error::invalid_json_exception();

				return json_traits::as_object(val);
			};

			/**
			 * Check if a claim is present in the map
			 * \return true if claim was present, false otherwise
			 */
			bool has_claim(const typename json_traits::string_type& name) const noexcept {
				return claims.count(name) != 0;
			}

			/**
			 * Get a claim by name
			 *
			 * \param name the name of the desired claim
			 * \return Requested claim
			 * \throw jwt::error::claim_not_present_exception if the claim was not present
			 */
			basic_claim_t get_claim(const typename json_traits::string_type& name) const {
				if (!has_claim(name)) throw error::claim_not_present_exception();
				return basic_claim_t{claims.at(name)};
			}

			std::unordered_map<typename json_traits::string_type, basic_claim_t> get_claims() const {
				static_assert(
					details::is_valid_json_object<typename json_traits::value_type, typename json_traits::string_type,
												  typename json_traits::object_type>::supports_claims_transform,
					"currently there is a limitation on the internal implemantation of the `object_type` to have an "
					"`std::pair` like `value_type`");

				std::unordered_map<typename json_traits::string_type, basic_claim_t> res;
				std::transform(claims.begin(), claims.end(), std::inserter(res, res.end()),
							   [](const typename json_traits::object_type::value_type& val) {
								   return std::make_pair(val.first, basic_claim_t{val.second});
							   });
				return res;
			}
		};
	} // namespace details

	/**
	 * Base class that represents a token payload.
	 * Contains Convenience accessors for common claims.
	 */
	template<typename json_traits>
	class payload {
	protected:
		details::map_of_claims<json_traits> payload_claims;

	public:
		using basic_claim_t = basic_claim<json_traits>;

		/**
		 * Check if issuer is present ("iss")
		 * \return true if present, false otherwise
		 */
		bool has_issuer() const noexcept { return has_payload_claim("iss"); }
		/**
		 * Check if subject is present ("sub")
		 * \return true if present, false otherwise
		 */
		bool has_subject() const noexcept { return has_payload_claim("sub"); }
		/**
		 * Check if audience is present ("aud")
		 * \return true if present, false otherwise
		 */
		bool has_audience() const noexcept { return has_payload_claim("aud"); }
		/**
		 * Check if expires is present ("exp")
		 * \return true if present, false otherwise
		 */
		bool has_expires_at() const noexcept { return has_payload_claim("exp"); }
		/**
		 * Check if not before is present ("nbf")
		 * \return true if present, false otherwise
		 */
		bool has_not_before() const noexcept { return has_payload_claim("nbf"); }
		/**
		 * Check if issued at is present ("iat")
		 * \return true if present, false otherwise
		 */
		bool has_issued_at() const noexcept { return has_payload_claim("iat"); }
		/**
		 * Check if token id is present ("jti")
		 * \return true if present, false otherwise
		 */
		bool has_id() const noexcept { return has_payload_claim("jti"); }
		/**
		 * Get issuer claim
		 * \return issuer as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_issuer() const { return get_payload_claim("iss").as_string(); }
		/**
		 * Get subject claim
		 * \return subject as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_subject() const { return get_payload_claim("sub").as_string(); }
		/**
		 * Get audience claim
		 * \return audience as a set of strings
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a set (Should not happen in a valid token)
		 */
		typename basic_claim_t::set_t get_audience() const {
			auto aud = get_payload_claim("aud");
			if (aud.get_type() == json::type::string) return {aud.as_string()};

			return aud.as_set();
		}
		/**
		 * Get expires claim
		 * \return expires as a date in utc
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a date (Should not happen in a valid token)
		 */
		date get_expires_at() const { return get_payload_claim("exp").as_date(); }
		/**
		 * Get not valid before claim
		 * \return nbf date in utc
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a date (Should not happen in a valid token)
		 */
		date get_not_before() const { return get_payload_claim("nbf").as_date(); }
		/**
		 * Get issued at claim
		 * \return issued at as date in utc
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a date (Should not happen in a valid token)
		 */
		date get_issued_at() const { return get_payload_claim("iat").as_date(); }
		/**
		 * Get id claim
		 * \return id as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_id() const { return get_payload_claim("jti").as_string(); }
		/**
		 * Check if a payload claim is present
		 * \return true if claim was present, false otherwise
		 */
		bool has_payload_claim(const typename json_traits::string_type& name) const noexcept {
			return payload_claims.has_claim(name);
		}
		/**
		 * Get payload claim
		 * \return Requested claim
		 * \throw std::runtime_error If claim was not present
		 */
		basic_claim_t get_payload_claim(const typename json_traits::string_type& name) const {
			return payload_claims.get_claim(name);
		}
	};

	/**
	 * Base class that represents a token header.
	 * Contains Convenience accessors for common claims.
	 */
	template<typename json_traits>
	class header {
	protected:
		details::map_of_claims<json_traits> header_claims;

	public:
		using basic_claim_t = basic_claim<json_traits>;
		/**
		 * Check if algortihm is present ("alg")
		 * \return true if present, false otherwise
		 */
		bool has_algorithm() const noexcept { return has_header_claim("alg"); }
		/**
		 * Check if type is present ("typ")
		 * \return true if present, false otherwise
		 */
		bool has_type() const noexcept { return has_header_claim("typ"); }
		/**
		 * Check if content type is present ("cty")
		 * \return true if present, false otherwise
		 */
		bool has_content_type() const noexcept { return has_header_claim("cty"); }
		/**
		 * Check if key id is present ("kid")
		 * \return true if present, false otherwise
		 */
		bool has_key_id() const noexcept { return has_header_claim("kid"); }
		/**
		 * Get algorithm claim
		 * \return algorithm as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_algorithm() const { return get_header_claim("alg").as_string(); }
		/**
		 * Get type claim
		 * \return type as a string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_type() const { return get_header_claim("typ").as_string(); }
		/**
		 * Get content type claim
		 * \return content type as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_content_type() const { return get_header_claim("cty").as_string(); }
		/**
		 * Get key id claim
		 * \return key id as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_key_id() const { return get_header_claim("kid").as_string(); }
		/**
		 * Check if a header claim is present
		 * \return true if claim was present, false otherwise
		 */
		bool has_header_claim(const typename json_traits::string_type& name) const noexcept {
			return header_claims.has_claim(name);
		}
		/**
		 * Get header claim
		 * \return Requested claim
		 * \throw std::runtime_error If claim was not present
		 */
		basic_claim_t get_header_claim(const typename json_traits::string_type& name) const {
			return header_claims.get_claim(name);
		}
	};

	/**
	 * Class containing all information about a decoded token
	 */
	template<typename json_traits>
	class decoded_jwt : public header<json_traits>, public payload<json_traits> {
	protected:
		/// Unmodifed token, as passed to constructor
		const typename json_traits::string_type token;
		/// Header part decoded from base64
		typename json_traits::string_type header;
		/// Unmodified header part in base64
		typename json_traits::string_type header_base64;
		/// Payload part decoded from base64
		typename json_traits::string_type payload;
		/// Unmodified payload part in base64
		typename json_traits::string_type payload_base64;
		/// Signature part decoded from base64
		typename json_traits::string_type signature;
		/// Unmodified signature part in base64
		typename json_traits::string_type signature_base64;

	public:
		using basic_claim_t = basic_claim<json_traits>;
#ifndef JWT_DISABLE_BASE64
		/**
		 * \brief Parses a given token
		 *
		 * \note Decodes using the jwt::base64url which supports an std::string
		 *
		 * \param token The token to parse
		 * \throw std::invalid_argument Token is not in correct format
		 * \throw std::runtime_error Base64 decoding failed or invalid json
		 */
		JWT_CLAIM_EXPLICIT decoded_jwt(const typename json_traits::string_type& token)
			: decoded_jwt(token, [](const typename json_traits::string_type& token) {
				  return base::decode<alphabet::base64url>(base::pad<alphabet::base64url>(token));
			  }) {}
#endif
		/**
		 * \brief Parses a given token
		 *
		 * \tparam Decode is callabled, taking a string_type and returns a string_type.
		 * It should ensure the padding of the input and then base64url decode and
		 * return the results.
		 * \param token The token to parse
		 * \param decode The function to decode the token
		 * \throw std::invalid_argument Token is not in correct format
		 * \throw std::runtime_error Base64 decoding failed or invalid json
		 */
		template<typename Decode>
		decoded_jwt(const typename json_traits::string_type& token, Decode decode) : token(token) {
			auto hdr_end = token.find('.');
			if (hdr_end == json_traits::string_type::npos) throw std::invalid_argument("invalid token supplied");
			auto payload_end = token.find('.', hdr_end + 1);
			if (payload_end == json_traits::string_type::npos) throw std::invalid_argument("invalid token supplied");
			header_base64 = token.substr(0, hdr_end);
			payload_base64 = token.substr(hdr_end + 1, payload_end - hdr_end - 1);
			signature_base64 = token.substr(payload_end + 1);

			header = decode(header_base64);
			payload = decode(payload_base64);
			signature = decode(signature_base64);

			this->header_claims = details::map_of_claims<json_traits>::parse_claims(header);
			this->payload_claims = details::map_of_claims<json_traits>::parse_claims(payload);
		}

		/**
		 * Get token string, as passed to constructor
		 * \return token as passed to constructor
		 */
		const typename json_traits::string_type& get_token() const noexcept { return token; }
		/**
		 * Get header part as json string
		 * \return header part after base64 decoding
		 */
		const typename json_traits::string_type& get_header() const noexcept { return header; }
		/**
		 * Get payload part as json string
		 * \return payload part after base64 decoding
		 */
		const typename json_traits::string_type& get_payload() const noexcept { return payload; }
		/**
		 * Get signature part as json string
		 * \return signature part after base64 decoding
		 */
		const typename json_traits::string_type& get_signature() const noexcept { return signature; }
		/**
		 * Get header part as base64 string
		 * \return header part before base64 decoding
		 */
		const typename json_traits::string_type& get_header_base64() const noexcept { return header_base64; }
		/**
		 * Get payload part as base64 string
		 * \return payload part before base64 decoding
		 */
		const typename json_traits::string_type& get_payload_base64() const noexcept { return payload_base64; }
		/**
		 * Get signature part as base64 string
		 * \return signature part before base64 decoding
		 */
		const typename json_traits::string_type& get_signature_base64() const noexcept { return signature_base64; }
		/**
		 * Get all payload claims
		 * \return map of claims
		 */
		std::unordered_map<typename json_traits::string_type, basic_claim_t> get_payload_claims() const {
			return this->payload_claims.get_claims();
		}
		/**
		 * Get all header claims
		 * \return map of claims
		 */
		std::unordered_map<typename json_traits::string_type, basic_claim_t> get_header_claims() const {
			return this->header_claims.get_claims();
		}
	};

	/**
	 * Builder class to build and sign a new token
	 * Use jwt::create() to get an instance of this class.
	 */
	template<typename json_traits>
	class builder {
		typename json_traits::object_type header_claims;
		typename json_traits::object_type payload_claims;

	public:
		builder() = default;
		/**
		 * Set a header claim.
		 * \param id Name of the claim
		 * \param c Claim to add
		 * \return *this to allow for method chaining
		 */
		builder& set_header_claim(const typename json_traits::string_type& id, typename json_traits::value_type c) {
			header_claims[id] = std::move(c);
			return *this;
		}

		/**
		 * Set a header claim.
		 * \param id Name of the claim
		 * \param c Claim to add
		 * \return *this to allow for method chaining
		 */
		builder& set_header_claim(const typename json_traits::string_type& id, basic_claim<json_traits> c) {
			header_claims[id] = c.to_json();
			return *this;
		}
		/**
		 * Set a payload claim.
		 * \param id Name of the claim
		 * \param c Claim to add
		 * \return *this to allow for method chaining
		 */
		builder& set_payload_claim(const typename json_traits::string_type& id, typename json_traits::value_type c) {
			payload_claims[id] = std::move(c);
			return *this;
		}
		/**
		 * Set a payload claim.
		 * \param id Name of the claim
		 * \param c Claim to add
		 * \return *this to allow for method chaining
		 */
		builder& set_payload_claim(const typename json_traits::string_type& id, basic_claim<json_traits> c) {
			payload_claims[id] = c.to_json();
			return *this;
		}
		/**
		 * \brief Set algorithm claim
		 * You normally don't need to do this, as the algorithm is automatically set if you don't change it.
		 *
		 * \param str Name of algorithm
		 * \return *this to allow for method chaining
		 */
		builder& set_algorithm(typename json_traits::string_type str) {
			return set_header_claim("alg", typename json_traits::value_type(str));
		}
		/**
		 * Set type claim
		 * \param str Type to set
		 * \return *this to allow for method chaining
		 */
		builder& set_type(typename json_traits::string_type str) {
			return set_header_claim("typ", typename json_traits::value_type(str));
		}
		/**
		 * Set content type claim
		 * \param str Type to set
		 * \return *this to allow for method chaining
		 */
		builder& set_content_type(typename json_traits::string_type str) {
			return set_header_claim("cty", typename json_traits::value_type(str));
		}
		/**
		 * \brief Set key id claim
		 *
		 * \param str Key id to set
		 * \return *this to allow for method chaining
		 */
		builder& set_key_id(typename json_traits::string_type str) {
			return set_header_claim("kid", typename json_traits::value_type(str));
		}
		/**
		 * Set issuer claim
		 * \param str Issuer to set
		 * \return *this to allow for method chaining
		 */
		builder& set_issuer(typename json_traits::string_type str) {
			return set_payload_claim("iss", typename json_traits::value_type(str));
		}
		/**
		 * Set subject claim
		 * \param str Subject to set
		 * \return *this to allow for method chaining
		 */
		builder& set_subject(typename json_traits::string_type str) {
			return set_payload_claim("sub", typename json_traits::value_type(str));
		}
		/**
		 * Set audience claim
		 * \param a Audience set
		 * \return *this to allow for method chaining
		 */
		builder& set_audience(typename json_traits::array_type a) {
			return set_payload_claim("aud", typename json_traits::value_type(a));
		}
		/**
		 * Set audience claim
		 * \param aud Single audience
		 * \return *this to allow for method chaining
		 */
		builder& set_audience(typename json_traits::string_type aud) {
			return set_payload_claim("aud", typename json_traits::value_type(aud));
		}
		/**
		 * Set expires at claim
		 * \param d Expires time
		 * \return *this to allow for method chaining
		 */
		builder& set_expires_at(const date& d) { return set_payload_claim("exp", basic_claim<json_traits>(d)); }
		/**
		 * Set not before claim
		 * \param d First valid time
		 * \return *this to allow for method chaining
		 */
		builder& set_not_before(const date& d) { return set_payload_claim("nbf", basic_claim<json_traits>(d)); }
		/**
		 * Set issued at claim
		 * \param d Issued at time, should be current time
		 * \return *this to allow for method chaining
		 */
		builder& set_issued_at(const date& d) { return set_payload_claim("iat", basic_claim<json_traits>(d)); }
		/**
		 * Set id claim
		 * \param str ID to set
		 * \return *this to allow for method chaining
		 */
		builder& set_id(const typename json_traits::string_type& str) {
			return set_payload_claim("jti", typename json_traits::value_type(str));
		}

		/**
		 * Sign token and return result
		 * \tparam Algo Callable method which takes a string_type and return the signed input as a string_type
		 * \tparam Encode Callable method which takes a string_type and base64url safe encodes it,
		 * MUST return the result with no padding; trim the result.
		 * \param algo Instance of an algorithm to sign the token with
		 * \param encode Callable to transform the serialized json to base64 with no padding
		 * \return Final token as a string
		 *
		 * \note If the 'alg' header in not set in the token it will be set to `algo.name()`
		 */
		template<typename Algo, typename Encode>
		typename json_traits::string_type sign(const Algo& algo, Encode encode) const {
			std::error_code ec;
			auto res = sign(algo, encode, ec);
			error::throw_if_error(ec);
			return res;
		}
#ifndef JWT_DISABLE_BASE64
		/**
		 * Sign token and return result
		 *
		 * using the `jwt::base` functions provided
		 *
		 * \param algo Instance of an algorithm to sign the token with
		 * \return Final token as a string
		 */
		template<typename Algo>
		typename json_traits::string_type sign(const Algo& algo) const {
			std::error_code ec;
			auto res = sign(algo, ec);
			error::throw_if_error(ec);
			return res;
		}
#endif

		/**
		 * Sign token and return result
		 * \tparam Algo Callable method which takes a string_type and return the signed input as a string_type
		 * \tparam Encode Callable method which takes a string_type and base64url safe encodes it,
		 * MUST return the result with no padding; trim the result.
		 * \param algo Instance of an algorithm to sign the token with
		 * \param encode Callable to transform the serialized json to base64 with no padding
		 * \param ec error_code filled with details on error
		 * \return Final token as a string
		 *
		 * \note If the 'alg' header in not set in the token it will be set to `algo.name()`
		 */
		template<typename Algo, typename Encode>
		typename json_traits::string_type sign(const Algo& algo, Encode encode, std::error_code& ec) const {
			// make a copy such that a builder can be re-used
			typename json_traits::object_type obj_header = header_claims;
			if (header_claims.count("alg") == 0) obj_header["alg"] = typename json_traits::value_type(algo.name());

			const auto header = encode(json_traits::serialize(typename json_traits::value_type(obj_header)));
			const auto payload = encode(json_traits::serialize(typename json_traits::value_type(payload_claims)));
			const auto token = header + "." + payload;

			auto signature = algo.sign(token, ec);
			if (ec) return {};

			return token + "." + encode(signature);
		}
#ifndef JWT_DISABLE_BASE64
		/**
		 * Sign token and return result
		 *
		 * using the `jwt::base` functions provided
		 *
		 * \param algo Instance of an algorithm to sign the token with
		 * \param ec error_code filled with details on error
		 * \return Final token as a string
		 */
		template<typename Algo>
		typename json_traits::string_type sign(const Algo& algo, std::error_code& ec) const {
			return sign(
				algo,
				[](const typename json_traits::string_type& data) {
					return base::trim<alphabet::base64url>(base::encode<alphabet::base64url>(data));
				},
				ec);
		}
#endif
	};

	namespace verify_ops {
		template<typename json_traits>
		struct verify_context {
			verify_context(date ctime, const decoded_jwt<json_traits>& j, size_t l)
				: current_time(ctime), jwt(j), default_leeway(l) {}
			// Current time, retrieved from the verifiers clock and cached for performance and consistency
			date current_time;
			// The jwt passed to the verifier
			const decoded_jwt<json_traits>& jwt;
			// The configured default leeway for this verification
			size_t default_leeway{0};

			// The claim key to apply this comparision on
			typename json_traits::string_type claim_key{};

			// Helper method to get a claim from the jwt in this context
			basic_claim<json_traits> get_claim(bool in_header, std::error_code& ec) const {
				if (in_header) {
					if (!jwt.has_header_claim(claim_key)) {
						ec = error::token_verification_error::missing_claim;
						return {};
					}
					return jwt.get_header_claim(claim_key);
				} else {
					if (!jwt.has_payload_claim(claim_key)) {
						ec = error::token_verification_error::missing_claim;
						return {};
					}
					return jwt.get_payload_claim(claim_key);
				}
			}
			basic_claim<json_traits> get_claim(bool in_header, json::type t, std::error_code& ec) const {
				auto c = get_claim(in_header, ec);
				if (ec) return {};
				if (c.get_type() != t) {
					ec = error::token_verification_error::claim_type_missmatch;
					return {};
				}
				return c;
			}
			basic_claim<json_traits> get_claim(std::error_code& ec) const { return get_claim(false, ec); }
			basic_claim<json_traits> get_claim(json::type t, std::error_code& ec) const {
				return get_claim(false, t, ec);
			}
		};

		/**
		 * This is the default operation and does case sensitive matching
		 */
		template<typename json_traits, bool in_header = false>
		struct equals_claim {
			const basic_claim<json_traits> expected;
			void operator()(const verify_context<json_traits>& ctx, std::error_code& ec) const {
				auto jc = ctx.get_claim(in_header, expected.get_type(), ec);
				if (ec) return;
				const bool matches = [&]() {
					switch (expected.get_type()) {
					case json::type::boolean: return expected.as_bool() == jc.as_bool();
					case json::type::integer: return expected.as_int() == jc.as_int();
					case json::type::number: return expected.as_number() == jc.as_number();
					case json::type::string: return expected.as_string() == jc.as_string();
					case json::type::array:
					case json::type::object:
						return json_traits::serialize(expected.to_json()) == json_traits::serialize(jc.to_json());
					default: throw std::logic_error("internal error, should be unreachable");
					}
				}();
				if (!matches) {
					ec = error::token_verification_error::claim_value_missmatch;
					return;
				}
			}
		};

		/**
		 * Checks that the current time is before the time specified in the given
		 * claim. This is identical to how the "exp" check works.
		 */
		template<typename json_traits, bool in_header = false>
		struct date_before_claim {
			const size_t leeway;
			void operator()(const verify_context<json_traits>& ctx, std::error_code& ec) const {
				auto jc = ctx.get_claim(in_header, json::type::integer, ec);
				if (ec) return;
				auto c = jc.as_date();
				if (ctx.current_time > c + std::chrono::seconds(leeway)) {
					ec = error::token_verification_error::token_expired;
				}
			}
		};

		/**
		 * Checks that the current time is after the time specified in the given
		 * claim. This is identical to how the "nbf" and "iat" check works.
		 */
		template<typename json_traits, bool in_header = false>
		struct date_after_claim {
			const size_t leeway;
			void operator()(const verify_context<json_traits>& ctx, std::error_code& ec) const {
				auto jc = ctx.get_claim(in_header, json::type::integer, ec);
				if (ec) return;
				auto c = jc.as_date();
				if (ctx.current_time < c - std::chrono::seconds(leeway)) {
					ec = error::token_verification_error::token_expired;
				}
			}
		};

		/**
		 * Checks if the given set is a subset of the set inside the token.
		 * If the token value is a string it is traited as a set of a single element.
		 * The comparison is case sensitive.
		 */
		template<typename json_traits, bool in_header = false>
		struct is_subset_claim {
			const typename basic_claim<json_traits>::set_t expected;
			void operator()(const verify_context<json_traits>& ctx, std::error_code& ec) const {
				auto c = ctx.get_claim(in_header, ec);
				if (ec) return;
				if (c.get_type() == json::type::string) {
					if (expected.size() != 1 || *expected.begin() != c.as_string()) {
						ec = error::token_verification_error::audience_missmatch;
						return;
					}
				} else if (c.get_type() == json::type::array) {
					auto jc = c.as_set();
					for (auto& e : expected) {
						if (jc.find(e) == jc.end()) {
							ec = error::token_verification_error::audience_missmatch;
							return;
						}
					}
				} else {
					ec = error::token_verification_error::claim_type_missmatch;
					return;
				}
			}
		};

		/**
		 * Checks if the claim is a string and does an case insensitive comparison.
		 */
		template<typename json_traits, bool in_header = false>
		struct insensitive_string_claim {
			const typename json_traits::string_type expected;
			std::locale locale;
			insensitive_string_claim(const typename json_traits::string_type& e, std::locale loc)
				: expected(to_lower_unicode(e, loc)), locale(loc) {}

			void operator()(const verify_context<json_traits>& ctx, std::error_code& ec) const {
				const auto c = ctx.get_claim(in_header, json::type::string, ec);
				if (ec) return;
				if (to_lower_unicode(c.as_string(), locale) != expected) {
					ec = error::token_verification_error::claim_value_missmatch;
				}
			}

			static std::string to_lower_unicode(const std::string& str, const std::locale& loc) {
				std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> conv;
				auto wide = conv.from_bytes(str);
				auto& f = std::use_facet<std::ctype<wchar_t>>(loc);
				f.toupper(&wide[0], &wide[0] + wide.size());
				return conv.to_bytes(wide);
			}
		};
	} // namespace verify_ops

	/**
	 * Verifier class used to check if a decoded token contains all claims required by your application and has a valid
	 * signature.
	 */
	template<typename Clock, typename json_traits>
	class verifier {
	public:
		using basic_claim_t = basic_claim<json_traits>;
		/**
		 * Verification function
		 *
		 * This gets passed the current verifier, a reference to the decoded jwt, a reference to the key of this claim,
		 * as well as a reference to an error_code.
		 * The function checks if the actual value matches certain rules (e.g. equality to value x) and sets the error_code if
		 * it does not. Once a non zero error_code is encountered the verification stops and this error_code becomes the result
		 * returned from verify
		 */
		using verify_check_fn_t =
			std::function<void(const verify_ops::verify_context<json_traits>&, std::error_code& ec)>;

	private:
		struct algo_base {
			virtual ~algo_base() = default;
			virtual void verify(const std::string& data, const std::string& sig, std::error_code& ec) = 0;
		};
		template<typename T>
		struct algo : public algo_base {
			T alg;
			explicit algo(T a) : alg(a) {}
			void verify(const std::string& data, const std::string& sig, std::error_code& ec) override {
				alg.verify(data, sig, ec);
			}
		};
		/// Required claims
		std::unordered_map<typename json_traits::string_type, verify_check_fn_t> claims;
		/// Leeway time for exp, nbf and iat
		size_t default_leeway = 0;
		/// Instance of clock type
		Clock clock;
		/// Supported algorithms
		std::unordered_map<std::string, std::shared_ptr<algo_base>> algs;

	public:
		/**
		 * Constructor for building a new verifier instance
		 * \param c Clock instance
		 */
		explicit verifier(Clock c) : clock(c) {
			claims["exp"] = [](const verify_ops::verify_context<json_traits>& ctx, std::error_code& ec) {
				if (!ctx.jwt.has_expires_at()) return;
				auto exp = ctx.jwt.get_expires_at();
				if (ctx.current_time > exp + std::chrono::seconds(ctx.default_leeway)) {
					ec = error::token_verification_error::token_expired;
				}
			};
			claims["iat"] = [](const verify_ops::verify_context<json_traits>& ctx, std::error_code& ec) {
				if (!ctx.jwt.has_issued_at()) return;
				auto iat = ctx.jwt.get_issued_at();
				if (ctx.current_time < iat - std::chrono::seconds(ctx.default_leeway)) {
					ec = error::token_verification_error::token_expired;
				}
			};
			claims["nbf"] = [](const verify_ops::verify_context<json_traits>& ctx, std::error_code& ec) {
				if (!ctx.jwt.has_not_before()) return;
				auto nbf = ctx.jwt.get_not_before();
				if (ctx.current_time < nbf - std::chrono::seconds(ctx.default_leeway)) {
					ec = error::token_verification_error::token_expired;
				}
			};
		}

		/**
		 * Set default leeway to use.
		 * \param leeway Default leeway to use if not specified otherwise
		 * \return *this to allow chaining
		 */
		verifier& leeway(size_t leeway) {
			default_leeway = leeway;
			return *this;
		}
		/**
		 * Set leeway for expires at.
		 * If not specified the default leeway will be used.
		 * \param leeway Set leeway to use for expires at.
		 * \return *this to allow chaining
		 */
		verifier& expires_at_leeway(size_t leeway) {
			claims["exp"] = verify_ops::date_before_claim<json_traits>{leeway};
			return *this;
		}
		/**
		 * Set leeway for not before.
		 * If not specified the default leeway will be used.
		 * \param leeway Set leeway to use for not before.
		 * \return *this to allow chaining
		 */
		verifier& not_before_leeway(size_t leeway) {
			claims["nbf"] = verify_ops::date_after_claim<json_traits>{leeway};
			return *this;
		}
		/**
		 * Set leeway for issued at.
		 * If not specified the default leeway will be used.
		 * \param leeway Set leeway to use for issued at.
		 * \return *this to allow chaining
		 */
		verifier& issued_at_leeway(size_t leeway) {
			claims["iat"] = verify_ops::date_after_claim<json_traits>{leeway};
			return *this;
		}

		/**
		 * Set an type to check for.
		 * Check is casesensitive.
		 * \param iss Issuer to check for.
		 * \return *this to allow chaining
		 */
		verifier& with_type(const typename json_traits::string_type& type, std::locale locale = std::locale{}) {
			return with_claim("typ", verify_ops::insensitive_string_claim<json_traits, true>{type, std::move(locale)});
		}

		/**
		 * Set an issuer to check for.
		 * Check is casesensitive.
		 * \param iss Issuer to check for.
		 * \return *this to allow chaining
		 */
		verifier& with_issuer(const typename json_traits::string_type& iss) {
			return with_claim("iss", basic_claim_t(iss));
		}

		/**
		 * Set a subject to check for.
		 * Check is casesensitive.
		 * \param sub Subject to check for.
		 * \return *this to allow chaining
		 */
		verifier& with_subject(const typename json_traits::string_type& sub) {
			return with_claim("sub", basic_claim_t(sub));
		}
		/**
		 * Set an audience to check for.
		 * If any of the specified audiences is not present in the token the check fails.
		 * \param aud Audience to check for.
		 * \return *this to allow chaining
		 */
		verifier& with_audience(const typename basic_claim_t::set_t& aud) {
			claims["aud"] = verify_ops::is_subset_claim<json_traits>{aud};
			return *this;
		}
		/**
		 * Set an audience to check for.
		 * If the specified audiences is not present in the token the check fails.
		 * \param aud Audience to check for.
		 * \return *this to allow chaining
		 */
		verifier& with_audience(const typename json_traits::string_type& aud) {
			typename basic_claim_t::set_t s;
			s.insert(aud);
			return with_audience(s);
		}
		/**
		 * Set an id to check for.
		 * Check is casesensitive.
		 * \param id ID to check for.
		 * \return *this to allow chaining
		 */
		verifier& with_id(const typename json_traits::string_type& id) { return with_claim("jti", basic_claim_t(id)); }

		/**
		 * Specify a claim to check for using the specified operation.
		 * \param name Name of the claim to check for
		 * \param fn Function to use for verifying the claim
		 * \return *this to allow chaining
		 */
		verifier& with_claim(const typename json_traits::string_type& name, verify_check_fn_t fn) {
			claims[name] = fn;
			return *this;
		}

		/**
		 * Specify a claim to check for equality (both type & value).
		 * \param name Name of the claim to check for
		 * \param c Claim to check for
		 * \return *this to allow chaining
		 */
		verifier& with_claim(const typename json_traits::string_type& name, basic_claim_t c) {
			return with_claim(name, verify_ops::equals_claim<json_traits>{c});
		}

		/**
		 * Add an algorithm available for checking.
		 * \param alg Algorithm to allow
		 * \return *this to allow chaining
		 */
		template<typename Algorithm>
		verifier& allow_algorithm(Algorithm alg) {
			algs[alg.name()] = std::make_shared<algo<Algorithm>>(alg);
			return *this;
		}

		/**
		 * Verify the given token.
		 * \param jwt Token to check
		 * \throw token_verification_exception Verification failed
		 */
		void verify(const decoded_jwt<json_traits>& jwt) const {
			std::error_code ec;
			verify(jwt, ec);
			error::throw_if_error(ec);
		}
		/**
		 * Verify the given token.
		 * \param jwt Token to check
		 * \param ec error_code filled with details on error
		 */
		void verify(const decoded_jwt<json_traits>& jwt, std::error_code& ec) const {
			ec.clear();
			const typename json_traits::string_type data = jwt.get_header_base64() + "." + jwt.get_payload_base64();
			const typename json_traits::string_type sig = jwt.get_signature();
			const std::string algo = jwt.get_algorithm();
			if (algs.count(algo) == 0) {
				ec = error::token_verification_error::wrong_algorithm;
				return;
			}
			algs.at(algo)->verify(data, sig, ec);
			if (ec) return;

			verify_ops::verify_context<json_traits> ctx{clock.now(), jwt, default_leeway};
			for (auto& c : claims) {
				ctx.claim_key = c.first;
				c.second(ctx, ec);
				if (ec) return;
			}
		}
	};

	/**
	 * \brief JSON Web Key
	 *
	 * https://tools.ietf.org/html/rfc7517
	 *
	 * A JSON object that represents a cryptographic key.  The members of
	 * the object represent properties of the key, including its value.
	 */
	template<typename json_traits>
	class jwk {
		using basic_claim_t = basic_claim<json_traits>;
		const details::map_of_claims<json_traits> jwk_claims;

	public:
		JWT_CLAIM_EXPLICIT jwk(const typename json_traits::string_type& str)
			: jwk_claims(details::map_of_claims<json_traits>::parse_claims(str)) {}

		JWT_CLAIM_EXPLICIT jwk(const typename json_traits::value_type& json)
			: jwk_claims(json_traits::as_object(json)) {}

		/**
		 * Get key type claim
		 *
		 * This returns the general type (e.g. RSA or EC), not a specific algorithm value.
		 * \return key type as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_key_type() const { return get_jwk_claim("kty").as_string(); }

		/**
		 * Get public key usage claim
		 * \return usage parameter as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_use() const { return get_jwk_claim("use").as_string(); }

		/**
		 * Get key operation types claim
		 * \return key operation types as a set of strings
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename basic_claim_t::set_t get_key_operations() const { return get_jwk_claim("key_ops").as_set(); }

		/**
		 * Get algorithm claim
		 * \return algorithm as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_algorithm() const { return get_jwk_claim("alg").as_string(); }

		/**
		 * Get key id claim
		 * \return key id as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_key_id() const { return get_jwk_claim("kid").as_string(); }

		/**
		 * \brief Get curve claim
		 *
		 * https://www.rfc-editor.org/rfc/rfc7518.html#section-6.2.1.1
		 * https://www.iana.org/assignments/jose/jose.xhtml#table-web-key-elliptic-curve
		 *
		 * \return curve as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_curve() const { return get_jwk_claim("crv").as_string(); }

		/**
		 * Get x5c claim
		 * \return x5c as an array
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a array (Should not happen in a valid token)
		 */
		typename json_traits::array_type get_x5c() const { return get_jwk_claim("x5c").as_array(); };

		/**
		 * Get X509 URL claim
		 * \return x5u as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_x5u() const { return get_jwk_claim("x5u").as_string(); };

		/**
		 * Get X509 thumbprint claim
		 * \return x5t as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_x5t() const { return get_jwk_claim("x5t").as_string(); };

		/**
		 * Get X509 SHA256 thumbprint claim
		 * \return x5t#S256 as string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_x5t_sha256() const { return get_jwk_claim("x5t#S256").as_string(); };

		/**
		 * Get x5c claim as a string
		 * \return x5c as an string
		 * \throw std::runtime_error If claim was not present
		 * \throw std::bad_cast Claim was present but not a string (Should not happen in a valid token)
		 */
		typename json_traits::string_type get_x5c_key_value() const {
			auto x5c_array = get_jwk_claim("x5c").as_array();
			if (x5c_array.size() == 0) throw error::claim_not_present_exception();

			return json_traits::as_string(x5c_array.front());
		};

		/**
		 * Check if a key type is present ("kty")
		 * \return true if present, false otherwise
		 */
		bool has_key_type() const noexcept { return has_jwk_claim("kty"); }

		/**
		 * Check if a public key usage indication is present ("use")
		 * \return true if present, false otherwise
		 */
		bool has_use() const noexcept { return has_jwk_claim("use"); }

		/**
		 * Check if a key operations parameter is present ("key_ops")
		 * \return true if present, false otherwise
		 */
		bool has_key_operations() const noexcept { return has_jwk_claim("key_ops"); }

		/**
		 * Check if algortihm is present ("alg")
		 * \return true if present, false otherwise
		 */
		bool has_algorithm() const noexcept { return has_jwk_claim("alg"); }

		/**
		 * Check if curve is present ("crv")
		 * \return true if present, false otherwise
		 */
		bool has_curve() const noexcept { return has_jwk_claim("crv"); }

		/**
		 * Check if key id is present ("kid")
		 * \return true if present, false otherwise
		 */
		bool has_key_id() const noexcept { return has_jwk_claim("kid"); }

		/**
		 * Check if X509 URL is present ("x5u")
		 * \return true if present, false otherwise
		 */
		bool has_x5u() const noexcept { return has_jwk_claim("x5u"); }

		/**
		 * Check if X509 Chain is present ("x5c")
		 * \return true if present, false otherwise
		 */
		bool has_x5c() const noexcept { return has_jwk_claim("x5c"); }

		/**
		 * Check if a X509 thumbprint is present ("x5t")
		 * \return true if present, false otherwise
		 */
		bool has_x5t() const noexcept { return has_jwk_claim("x5t"); }

		/**
		 * Check if a X509 SHA256 thumbprint is present ("x5t#S256")
		 * \return true if present, false otherwise
		 */
		bool has_x5t_sha256() const noexcept { return has_jwk_claim("x5t#S256"); }

		/**
		 * Check if a jwks claim is present
		 * \return true if claim was present, false otherwise
		 */
		bool has_jwk_claim(const typename json_traits::string_type& name) const noexcept {
			return jwk_claims.has_claim(name);
		}

		/**
		 * Get jwks claim
		 * \return Requested claim
		 * \throw std::runtime_error If claim was not present
		 */
		basic_claim_t get_jwk_claim(const typename json_traits::string_type& name) const {
			return jwk_claims.get_claim(name);
		}

		bool empty() const noexcept { return jwk_claims.empty(); }
	};

	/**
	 * \brief JWK Set
	 *
	 * https://tools.ietf.org/html/rfc7517
	 *
	 * A JSON object that represents a set of JWKs.  The JSON object MUST
	 * have a "keys" member, which is an array of JWKs.
	 *
	 * This container takes a JWKs and simplifies it to a vector of JWKs
	 */
	template<typename json_traits>
	class jwks {
	public:
		using jwk_t = jwk<json_traits>;
		using jwt_vector_t = std::vector<jwk_t>;
		using iterator = typename jwt_vector_t::iterator;
		using const_iterator = typename jwt_vector_t::const_iterator;

		JWT_CLAIM_EXPLICIT jwks(const typename json_traits::string_type& str) {
			typename json_traits::value_type val;
			if (!json_traits::parse(val, str)) throw error::invalid_json_exception();

			const details::map_of_claims<json_traits> jwks_json = json_traits::as_object(val);
			if (!jwks_json.has_claim("keys")) throw error::invalid_json_exception();

			auto jwk_list = jwks_json.get_claim("keys").as_array();
			std::transform(jwk_list.begin(), jwk_list.end(), std::back_inserter(jwk_claims),
						   [](const typename json_traits::value_type& val) { return jwk_t{val}; });
		}

		iterator begin() { return jwk_claims.begin(); }
		iterator end() { return jwk_claims.end(); }
		const_iterator cbegin() const { return jwk_claims.begin(); }
		const_iterator cend() const { return jwk_claims.end(); }
		const_iterator begin() const { return jwk_claims.begin(); }
		const_iterator end() const { return jwk_claims.end(); }

		/**
		 * Check if a jwk with the kid is present
		 * \return true if jwk was present, false otherwise
		 */
		bool has_jwk(const typename json_traits::string_type& key_id) const noexcept {
			return find_by_kid(key_id) != end();
		}

		/**
		 * Get jwk
		 * \return Requested jwk by key_id
		 * \throw std::runtime_error If jwk was not present
		 */
		jwk_t get_jwk(const typename json_traits::string_type& key_id) const {
			const auto maybe = find_by_kid(key_id);
			if (maybe == end()) throw error::claim_not_present_exception();
			return *maybe;
		}

	private:
		jwt_vector_t jwk_claims;

		const_iterator find_by_kid(const typename json_traits::string_type& key_id) const noexcept {
			return std::find_if(cbegin(), cend(), [key_id](const jwk_t& jwk) {
				if (!jwk.has_key_id()) { return false; }
				return jwk.get_key_id() == key_id;
			});
		}
	};

	/**
	 * Create a verifier using the given clock
	 * \param c Clock instance to use
	 * \return verifier instance
	 */
	template<typename Clock, typename json_traits>
	verifier<Clock, json_traits> verify(Clock c) {
		return verifier<Clock, json_traits>(c);
	}

	/**
	 * Default clock class using std::chrono::system_clock as a backend.
	 */
	struct default_clock {
		date now() const { return date::clock::now(); }
	};

	/**
	 * Return a builder instance to create a new token
	 */
	template<typename json_traits>
	builder<json_traits> create() {
		return builder<json_traits>();
	}

	/**
	 * Decode a token
	 * \param token Token to decode
	 * \param decode function that will pad and base64url decode the token
	 * \return Decoded token
	 * \throw std::invalid_argument Token is not in correct format
	 * \throw std::runtime_error Base64 decoding failed or invalid json
	 */
	template<typename json_traits, typename Decode>
	decoded_jwt<json_traits> decode(const typename json_traits::string_type& token, Decode decode) {
		return decoded_jwt<json_traits>(token, decode);
	}

	/**
	 * Decode a token
	 * \param token Token to decode
	 * \return Decoded token
	 * \throw std::invalid_argument Token is not in correct format
	 * \throw std::runtime_error Base64 decoding failed or invalid json
	 */
	template<typename json_traits>
	decoded_jwt<json_traits> decode(const typename json_traits::string_type& token) {
		return decoded_jwt<json_traits>(token);
	}

	template<typename json_traits>
	jwk<json_traits> parse_jwk(const typename json_traits::string_type& token) {
		return jwk<json_traits>(token);
	}

	template<typename json_traits>
	jwks<json_traits> parse_jwks(const typename json_traits::string_type& token) {
		return jwks<json_traits>(token);
	}

#ifndef JWT_DISABLE_PICOJSON
	struct picojson_traits {
		using value_type = picojson::value;
		using object_type = picojson::object;
		using array_type = picojson::array;
		using string_type = std::string;
		using number_type = double;
		using integer_type = int64_t;
		using boolean_type = bool;

		static json::type get_type(const picojson::value& val) {
			using json::type;
			if (val.is<bool>()) return type::boolean;
			if (val.is<int64_t>()) return type::integer;
			if (val.is<double>()) return type::number;
			if (val.is<std::string>()) return type::string;
			if (val.is<picojson::array>()) return type::array;
			if (val.is<picojson::object>()) return type::object;

			throw std::logic_error("invalid type");
		}

		static picojson::object as_object(const picojson::value& val) {
			if (!val.is<picojson::object>()) throw std::bad_cast();
			return val.get<picojson::object>();
		}

		static std::string as_string(const picojson::value& val) {
			if (!val.is<std::string>()) throw std::bad_cast();
			return val.get<std::string>();
		}

		static picojson::array as_array(const picojson::value& val) {
			if (!val.is<picojson::array>()) throw std::bad_cast();
			return val.get<picojson::array>();
		}

		static int64_t as_int(const picojson::value& val) {
			if (!val.is<int64_t>()) throw std::bad_cast();
			return val.get<int64_t>();
		}

		static bool as_bool(const picojson::value& val) {
			if (!val.is<bool>()) throw std::bad_cast();
			return val.get<bool>();
		}

		static double as_number(const picojson::value& val) {
			if (!val.is<double>()) throw std::bad_cast();
			return val.get<double>();
		}

		static bool parse(picojson::value& val, const std::string& str) { return picojson::parse(val, str).empty(); }

		static std::string serialize(const picojson::value& val) { return val.serialize(); }
	};

	/**
	 * Default JSON claim
	 *
	 * This type is the default specialization of the \ref basic_claim class which
	 * uses the standard template types.
	 */
	using claim = basic_claim<picojson_traits>;

	/**
	 * Create a verifier using the default clock
	 * \return verifier instance
	 */
	inline verifier<default_clock, picojson_traits> verify() {
		return verify<default_clock, picojson_traits>(default_clock{});
	}
	/**
	 * Return a picojson builder instance to create a new token
	 */
	inline builder<picojson_traits> create() { return builder<picojson_traits>(); }
#ifndef JWT_DISABLE_BASE64
	/**
	 * Decode a token
	 * \param token Token to decode
	 * \return Decoded token
	 * \throw std::invalid_argument Token is not in correct format
	 * \throw std::runtime_error Base64 decoding failed or invalid json
	 */
	inline decoded_jwt<picojson_traits> decode(const std::string& token) { return decoded_jwt<picojson_traits>(token); }
#endif
	/**
	 * Decode a token
	 * \tparam Decode is callabled, taking a string_type and returns a string_type.
	 * It should ensure the padding of the input and then base64url decode and
	 * return the results.
	 * \param token Token to decode
	 * \param decode The token to parse
	 * \return Decoded token
	 * \throw std::invalid_argument Token is not in correct format
	 * \throw std::runtime_error Base64 decoding failed or invalid json
	 */
	template<typename Decode>
	decoded_jwt<picojson_traits> decode(const std::string& token, Decode decode) {
		return decoded_jwt<picojson_traits>(token, decode);
	}
	/**
	 * Parse a jwk
	 * \param token JWK Token to parse
	 * \return Parsed JWK
	 * \throw std::runtime_error Token is not in correct format
	 */
	inline jwk<picojson_traits> parse_jwk(const picojson_traits::string_type& token) {
		return jwk<picojson_traits>(token);
	}

	/**
	 * Parse a jwks
	 * \param token JWKs Token to parse
	 * \return Parsed JWKs
	 * \throw std::runtime_error Token is not in correct format
	 */
	inline jwks<picojson_traits> parse_jwks(const picojson_traits::string_type& token) {
		return jwks<picojson_traits>(token);
	}
#endif
} // namespace jwt

template<typename json_traits>
std::istream& operator>>(std::istream& is, jwt::basic_claim<json_traits>& c) {
	return c.operator>>(is);
}

template<typename json_traits>
std::ostream& operator<<(std::ostream& os, const jwt::basic_claim<json_traits>& c) {
	return os << c.to_json();
}

#endif
