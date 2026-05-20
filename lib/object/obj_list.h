#pragma once

#include<cstring>
#include<cstddef>
#include<cassert>
#include<array>
#include<algorithm>
#include<new>

namespace pvz_emulator::object {

template<typename T, size_t S> class obj_list {
	class obj_wrap {
	protected:
		T t;
		size_t next_available;
		
		obj_wrap():t(), next_available(0) {}
		friend class obj_list;
	};

	class iterator {
	private:
		int i;
		obj_list& list;

	public:
		explicit iterator(int n, obj_list& l): i(n), list(l) {
			while (i < list.active_end &&
				(list.a[i].t.is_freeable() || list.a[i].next_available != i))
			{
				i++;
			}
		}
		
		iterator& operator++() {
			do {
				i++;
			} while (i < list.active_end &&
				(list.a[i].t.is_freeable() || list.a[i].next_available != i));
			return *this;
		}

		iterator operator++(int) {
			auto original = *this;
			++(*this);
			return original;
		}

		bool operator==(const iterator& other) const {
			if (other.i == i) {
				return true;
			} else if (i == -1) {
				return other.i >= list.active_end;
			} else if (other.i == -1) {
				return static_cast<size_t>(i) >= list.active_end;
			} else {
				return false;
			}
		}

		bool operator!=(const iterator& other) const {
			return !((*this) == other);
		}

		T& operator*() const {
			return list.a[i].t;
		}
	};

	obj_wrap a[S];
	size_t next_available;
	size_t active_end;
	size_t n_actives;
public:
	explicit obj_list() :
		next_available(0),
		active_end(0),
		n_actives(0)
	{
		memset(a, 0, sizeof(a));
	}

	T& alloc() {
		size_t i;

		if (next_available > S) {
			throw std::bad_alloc();
		}

		if (next_available >= active_end) {
			if (active_end < S) {
				i = active_end;
				next_available = ++active_end;
			} else {
				throw std::bad_alloc();
			}
		} else {
			i = next_available;
			next_available = std::min(a[i].next_available, active_end);
		}

		a[i].next_available = i;
		++n_actives;

		return a[i].t;
	}

	// 测试辅助：分配一个编号低于 ref_index 的空闲槽。
	T& alloc_before(int ref_index) {
		if (ref_index <= 0) {
			throw std::bad_alloc();
		}

		size_t prev = active_end;
		size_t curr = next_available;
		while (curr < active_end && curr >= static_cast<size_t>(ref_index)) {
			prev = curr;
			curr = std::min(a[curr].next_available, active_end);
		}
		if (curr >= active_end) {
			throw std::bad_alloc();
		}

		size_t next = std::min(a[curr].next_available, active_end);
		if (prev == active_end) {
			next_available = next;
		} else {
			a[prev].next_available = next;
		}

		a[curr].next_available = curr;
		++n_actives;

		return a[curr].t;
	}

	// 测试辅助：分配一个编号高于 ref_index 的槽，必要时追加新槽。
	T& alloc_after(int ref_index) {
		if (ref_index < 0 || ref_index >= static_cast<int>(S)) {
			throw std::bad_alloc();
		}

		size_t prev = active_end;
		size_t curr = next_available;
		while (curr < active_end && curr <= static_cast<size_t>(ref_index)) {
			prev = curr;
			curr = std::min(a[curr].next_available, active_end);
		}

		size_t i;
		if (curr < active_end) {
			i = curr;
			size_t next = std::min(a[curr].next_available, active_end);
			if (prev == active_end) {
				next_available = next;
			} else {
				a[prev].next_available = next;
			}
		} else if (active_end < S && active_end > static_cast<size_t>(ref_index)) {
			const size_t old_active_end = active_end;
			i = old_active_end;
			// 空闲链使用 active_end 作为链尾哨兵；保留低位空闲链并追加高位槽时，
			// 需要把旧哨兵同步到新的 active_end，避免新槽被误认为空闲槽。
			if (next_available >= old_active_end) {
				next_available = old_active_end + 1;
			} else {
				size_t tail = next_available;
				while (a[tail].next_available < old_active_end) {
					tail = a[tail].next_available;
				}
				a[tail].next_available = old_active_end + 1;
			}
			active_end = old_active_end + 1;
		} else {
			throw std::bad_alloc();
		}

		a[i].next_available = i;
		++n_actives;

		return a[i].t;
	}

	// 测试辅助：预留低编号死槽时优先占用最低可用槽。
	T& alloc_lowest() {
		if (next_available > S) {
			throw std::bad_alloc();
		}

		size_t i;
		if (next_available >= active_end) {
			if (active_end < S) {
				i = active_end;
				next_available = ++active_end;
			} else {
				throw std::bad_alloc();
			}
		} else {
			size_t best = active_end;
			size_t best_prev = active_end;
			size_t prev = active_end;
			size_t curr = next_available;
			while (curr < active_end) {
				if (best == active_end || curr < best) {
					best = curr;
					best_prev = prev;
				}

				prev = curr;
				curr = std::min(a[curr].next_available, active_end);
			}

			i = best;
			size_t next = std::min(a[best].next_available, active_end);
			if (best_prev == active_end) {
				next_available = next;
			} else {
				a[best_prev].next_available = next;
			}
		}

		a[i].next_available = i;
		++n_actives;

		return a[i].t;
	}

	T* get(int i) {
		if (i >= S || i < 0) {
			return nullptr;
		}

		if (a[i].next_available == i) {
			return &a[i].t;
		}

		return nullptr;
	}

	int get_index(const T& p) const {
		auto& t = reinterpret_cast<const obj_wrap&>(p);
		if (&t < &a[0] || &t > &a[S - 1]) {
			return -1;
		}

		return static_cast<int>(reinterpret_cast<const obj_wrap&>(p).next_available);
	}

	bool is_active(const T* p) const {
		auto ow = reinterpret_cast<const obj_wrap*>(p);
		if (ow < &a[0] || ow > &a[S - 1]) {
			return false;
		}

		auto i = ow->next_available;

		return ow - a == i;
	}

	void shrink_to_fit() {
		n_actives = 0;
		size_t last = 0;

		for (auto i = 0ul; i < active_end; i++) {
			if (a[i].next_available == i && !a[i].t.is_freeable()) {
				last = i + 1;
				n_actives++;
			}
		}

		next_available = active_end = last;

		for (auto i = 0ul; i < active_end; i++) {
			if (i == next_available) {
				continue;
			}

			if (a[i].next_available != i || a[i].t.is_freeable()) {
				a[i].next_available = next_available;
				next_available = i;
			}
		}

		assert(next_available <= active_end);
	}

	[[nodiscard]]
	size_t size() const {
		return n_actives;
	}

	iterator begin() {
		return iterator(0, *this);
	}

	iterator end() {
		return iterator(-1, *this);
	}

	void clear() {
		next_available = 0;
		active_end = 0;
		n_actives = 0;
		memset(a, 0, sizeof(a));
	}
};

};

