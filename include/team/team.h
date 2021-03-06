#pragma once

#include "event_loop.h"
#include "rendezvous.h"
#include "timer.h"
#include "channel.h"

namespace team {
	struct await_t : public rendezvous {
		bool done;

		await_t() : rendezvous(&team::loop), done(false) {}
	};

	// High-level async constructs

	template <typename T>
	class generator {
		coroutine *gen;

		channel<std::unique_ptr<T>> channel;
		bool running;

		protected:

		template<typename V>
		void yield(V&& v){
			channel.send(std::unique_ptr<T>(new T(std::forward<V>(v))));
		}
		void yield(std::unique_ptr<T> &&p){ channel.send(std::move(p)); }

		virtual void generate() = 0;

		bool over;

		private:
		void start() { generate(); over = true; channel.send(nullptr); }

		public:

		typedef T value_type;

		generator() : gen(
			new coroutine(std::bind(&generator::start, this), nullptr)
		), channel(1), running(false), over(false) { }

		auto operator()() -> decltype(channel.recv()) {
			if (!running) {
				running = true;
				loop.blockOnce(gen);
			}
			return channel.recv();
		}

		class iterator {
			generator *g;
			std::unique_ptr<value_type> last;

			public:
			iterator(generator &_g) : g(&_g), last((*g)()) {}
			iterator() : g(nullptr) {}

			value_type *operator++() {
				last = (*g)();
				return last.get();
			}
			value_type &operator*() { return *last.get(); }

			bool operator!=(iterator &other) {
				return !(
					g == other.g ||
					(!g && other.g->over && !other.last) ||
					(!other.g && g->over && !last)
				);
			}
		};

		iterator begin() { return iterator(*this); }
		iterator end() { return iterator(); }

	};

	namespace util {

		// Based on http://stackoverflow.com/a/7943765/84745 by KennyTM
		template <typename T>
		struct arg_type : public arg_type<decltype(&T::operator())> {};

		template <typename C, typename R, typename... Args>
		struct arg_type<R(C::*)(Args...) const> {
			typedef typename std::tuple_element<0, std::tuple<Args...>>::type type;
		};

		template <typename T> using cb_type = typename arg_type<typename arg_type<T>::type>::type;
	}

	template<typename T>
	class lambda_generator : public generator<T> {
		public:

		class yield_t {
			lambda_generator &g;
			public:
			typedef T argument_type;
			template<typename V>
			void operator()(V&& v) { g.yield(std::forward<V>(v)); }
			yield_t(lambda_generator &_g) : g(_g) {}
		};

		private:
		std::function<void(yield_t)> f;

		public:
		explicit lambda_generator(decltype(f)&& _f) : f(std::forward<decltype(f)>(_f)) {}
		virtual void generate() override { f(yield_t(*this)); }
	};

	template <typename T> using yield = typename lambda_generator<T>::yield_t;

	template<typename F>
	lambda_generator<
		typename util::arg_type<F>::type::argument_type
	> make_generator(F&& f) {
		return lambda_generator<
			// This is ugly. I want to specialize util::cb_type to use
			// argument_type if it exists but had trouble doing that.
			typename util::arg_type<F>::type::argument_type
		>(std::forward<F>(f));
	}
}

team::basic_rendezvous const __r{team::loop};

// Ugly. Open to ideas.

// async: Implies a lambda with default capture by reference
#define async __r << [&]()
// acall: Runs any callable (taking no arguments) asynchrounously
#define acall __r <<

// #define await rendezvous_t _r(&team::loop); auto *__r = &_r;
#define await for (team::await_t __r; !__r.done; __r.done = true)
