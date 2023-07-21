#pragma once

#include <CppUtils.h>

namespace sl
{

namespace _redblack {

enum class ColorType {
	Null, Red, Black
};

struct HookStruct {
	HookStruct()
	: parent(nullptr), left(nullptr), right(nullptr),
			predecessor(nullptr), successor(nullptr),
			color(ColorType::Null) { }

	HookStruct(const HookStruct &other) = delete;

	HookStruct &operator= (const HookStruct &other) = delete;

	void *parent;
	void *left;
	void *right;
	void *predecessor;
	void *successor;
	ColorType color;
};

struct NullAggregator {
	template<typename T>
	static bool Aggregate(T *node) {
		(void)node;
		return false;
	}

	template<typename S, typename T>
	static bool CheckInvariant(S &tree, T *node) {
		(void)tree;
		(void)node;
		return true;
	}
};

template<typename D, typename T, HookStruct T:: *Member, typename A>
struct TreeCrtpStruct {
protected:
	static HookStruct *h(T *item) {
		return &(item->*Member);
	}

public:
	static T *GetParent(T *item) {
		return static_cast<T *>(h(item)->parent);
	}

	static T *GetLeft(T *item) {
		return static_cast<T *>(h(item)->left);
	}
	static T *GetRight(T *item) {
		return static_cast<T *>(h(item)->right);
	}

	static T *Predecessor(T *item) {
		return static_cast<T *>(h(item)->predecessor);
	}
	static T *Successor(T *item) {
		return static_cast<T *>(h(item)->successor);
	}

	T *GetRoot() {
		return static_cast<T *>(root);
	}

private:
	static bool IsRed(T *node) {
		if(!node)
			return false;
		return h(node)->color == ColorType::Red;
	}
	static bool IsBlack(T *node) {
		if(!node)
			return true;
		return h(node)->color == ColorType::Black;
	}

public:
	TreeCrtpStruct()
	: root{nullptr} { }

	TreeCrtpStruct(const TreeCrtpStruct &other) = delete;

	TreeCrtpStruct &operator= (const TreeCrtpStruct &other) = delete;

	T *First() {
		T *current = GetRoot();
		if(!current)
			return nullptr;
		while(GetLeft(current))
			current = GetLeft(current);
		return current;
	}

protected:
	void InsertRoot(T *node) {
		root = node;

		AggregateNode(node);
		FixInsert(node);
	}

	void InsertLeft(T *parent, T *node) {
		h(parent)->left = node;
		h(node)->parent = parent;

		T *pred = Predecessor(parent);
		if(pred)
			h(pred)->successor = node;
		h(node)->predecessor = pred;
		h(node)->successor = parent;
		h(parent)->predecessor = node;

		AggregateNode(node);
		AggregatePath(parent);
		FixInsert(node);
	}

	void InsertRight(T *parent, T *node) {
		h(parent)->right = node;
		h(node)->parent = parent;

		T *succ = Successor(parent);
		h(parent)->successor = node;
		h(node)->predecessor = parent;
		h(node)->successor = succ;
		if(succ)
			h(succ)->predecessor = node;

		AggregateNode(node);
		AggregatePath(parent);
		FixInsert(node);
	}

private:
	void FixInsert(T *n) {
		T *parent = GetParent(n);
		if(parent == nullptr) {
			h(n)->color = ColorType::Black;
			return;
		}

		h(n)->color = ColorType::Red;
		if(h(parent)->color == ColorType::Black)
			return;

		T *grand = GetParent(parent);

		if(GetLeft(grand) == parent && IsRed(GetRight(grand))) {
			h(grand)->color = ColorType::Red;
			h(parent)->color = ColorType::Black;
			h(GetRight(grand))->color = ColorType::Black;

			FixInsert(grand);
			return;
		}else if(GetRight(grand) == parent && IsRed(GetLeft(grand))) {
			h(grand)->color = ColorType::Red;
			h(parent)->color = ColorType::Black;
			h(GetLeft(grand))->color = ColorType::Black;

			FixInsert(grand);
			return;
		}

		if(parent == GetLeft(grand)) {
			if(n == GetRight(parent)) {
				RotateLeft(n);
				RotateRight(n);
				h(n)->color = ColorType::Black;
			}else{
				RotateRight(parent);
				h(parent)->color = ColorType::Black;
			}
			h(grand)->color = ColorType::Red;
		}else{
			if(n == GetLeft(parent)) {
				RotateRight(n);
				RotateLeft(n);
				h(n)->color = ColorType::Black;
			}else{
				RotateLeft(parent);
				h(parent)->color = ColorType::Black;
			}
			h(grand)->color = ColorType::Red;
		}
	}

public:
	void Remove(T *node) {
		T *leftPtr = GetLeft(node);
		T *rightPtr = GetRight(node);

		if(!leftPtr) {
			RemoveHalfLeaf(node, rightPtr);
		}else if(!rightPtr) {
			RemoveHalfLeaf(node, leftPtr);
		}else{
			T *pred = Predecessor(node);
			RemoveHalfLeaf(pred, GetLeft(pred));
			ReplaceNode(node, pred);
		}
	}

private:
	void ReplaceNode(T *node, T *replacement) {
		T *parent = GetParent(node);
		T *left = GetLeft(node);
		T *right = GetRight(node);

		if(parent == nullptr) {
			root = replacement;
		}else if(node == GetLeft(parent)) {
			h(parent)->left = replacement;
		}else{
			h(parent)->right = replacement;
		}
		h(replacement)->parent = parent;
		h(replacement)->color = h(node)->color;

		h(replacement)->left = left;
		if(left)
			h(left)->parent = replacement;

		h(replacement)->right = right;
		if(right)
			h(right)->parent = replacement;

		if(Predecessor(node))
			h(Predecessor(node))->successor = replacement;
		h(replacement)->predecessor = Predecessor(node);
		h(replacement)->successor = Successor(node);
		if(Successor(node))
			h(Successor(node))->predecessor = replacement;

		h(node)->left = nullptr;
		h(node)->right = nullptr;
		h(node)->parent = nullptr;
		h(node)->predecessor = nullptr;
		h(node)->successor = nullptr;

		AggregateNode(replacement);
		AggregatePath(parent);
	}

	void RemoveHalfLeaf(T *node, T *child) {
		T *pred = Predecessor(node);
		T *succ = Successor(node);
		if(pred)
			h(pred)->successor = succ;
		if(succ)
			h(succ)->predecessor = pred;

		if(h(node)->color == ColorType::Black) {
			if(IsRed(child)) {
				h(child)->color = ColorType::Black;
			}else{
				FixRemove(node);
			}
		}

		T *parent = GetParent(node);
		if(!parent) {
			root = child;
		}else if(GetLeft(parent) == node) {
			h(parent)->left = child;
		}else{
			h(parent)->right = child;
		}
		if(child)
			h(child)->parent = parent;

		h(node)->left = nullptr;
		h(node)->right = nullptr;
		h(node)->parent = nullptr;
		h(node)->predecessor = nullptr;
		h(node)->successor = nullptr;

		if(parent)
			AggregatePath(parent);
	}

	void FixRemove(T *n) {

		T *parent = GetParent(n);
		if(parent == nullptr)
			return;

		T *s; 
		if(GetLeft(parent) == n) {
			if(h(GetRight(parent))->color == ColorType::Red) {
				T *x = GetRight(parent);
				RotateLeft(GetRight(parent));

				h(parent)->color = ColorType::Red;
				h(x)->color = ColorType::Black;
			}

			s = GetRight(parent);
		}else{
			if(h(GetLeft(parent))->color == ColorType::Red) {
				T *x = GetLeft(parent);
				RotateRight(x);

				h(parent)->color = ColorType::Red;
				h(x)->color = ColorType::Black;
			}

			s = GetLeft(parent);
		}

		if(IsBlack(GetLeft(s)) && IsBlack(GetRight(s))) {
			if(h(parent)->color == ColorType::Black) {
				h(s)->color = ColorType::Red;
				FixRemove(parent);
				return;
			}else{
				h(parent)->color = ColorType::Black;
				h(s)->color = ColorType::Red;
				return;
			}
		}

		auto parentColor = h(parent)->color;
		if(GetLeft(parent) == n) {
			if(IsRed(GetLeft(s)) && IsBlack(GetRight(s))) {
				T *child = GetLeft(s);
				RotateRight(child);

				h(s)->color = ColorType::Red;
				h(child)->color = ColorType::Black;

				s = child;
			}

			RotateLeft(s);
			h(parent)->color = ColorType::Black;
			h(s)->color = parentColor;
			h(GetRight(s))->color = ColorType::Black;
		}else{

			if(IsRed(GetRight(s)) && IsBlack(GetLeft(s))) {
				T *child = GetRight(s);
				RotateLeft(child);

				h(s)->color = ColorType::Red;
				h(child)->color = ColorType::Black;

				s = child;
			}

			RotateRight(s);
			h(parent)->color = ColorType::Black;
			h(s)->color = parentColor;
			h(GetLeft(s))->color = ColorType::Black;
		}
	}

private:
	void RotateLeft(T *n) {
		T *u = GetParent(n);
		T *v = GetLeft(n);
		T *w = GetParent(u);

		if(v != nullptr)
			h(v)->parent = u;
		h(u)->right = v;
		h(u)->parent = n;
		h(n)->left = u;
		h(n)->parent = w;

		if(w == nullptr) {
			root = n;
		}else if(GetLeft(w) == u) {
			h(w)->left = n;
		}else{
			h(w)->right = n;
		}

		AggregateNode(u);
		AggregateNode(n);
	}

	void RotateRight(T *n) {
		T *u = GetParent(n);
		T *v = GetRight(n);
		T *w = GetParent(u);

		if(v != nullptr)
			h(v)->parent = u;
		h(u)->left = v;
		h(u)->parent = n;
		h(n)->right = u;
		h(n)->parent = w;

		if(w == nullptr) {
			root = n;
		}else if(GetLeft(w) == u) {
			h(w)->left = n;
		}else{
			h(w)->right = n;
		}

		AggregateNode(u);
		AggregateNode(n);
	}

public:
	void AggregateNode(T *node) {
		A::Aggregate(node);
	}

	void AggregatePath(T *node) {
		T *current = node;
		while(current) {
			if(!A::Aggregate(current))
				break;
			current = GetParent(current);
		}
	}

private:
	bool CheckInvariant() {
		if(!root)
			return true;

		int blackDepth;
		T *minimal, *maximal;
		return CheckInvariant(GetRoot(), blackDepth, minimal, maximal);
	}

	bool CheckInvariant(T *node, int &blackDepth, T *&minimal, T *&maximal) {
		if(h(node)->color == ColorType::Red)
			if(!IsBlack(GetLeft(node)) || !IsBlack(GetRight(node))) {
				return false;
			}

		int leftBlackDepth = 0;
		int rightBlackDepth = 0;

		if(GetLeft(node)) {
			T *pred;
			if(!CheckInvariant(GetLeft(node), leftBlackDepth, minimal, pred))
				return false;

			if(Successor(pred) != node) {
				return false;
			}else if(Predecessor(node) != pred) {
				return false;
			}
		}else{
			minimal = node;
		}

		if(GetRight(node)) {

			T *succ;
			if(!CheckInvariant(GetRight(node), rightBlackDepth, succ, maximal))
				return false;

			if(Successor(node) != succ) {
				return false;
			}else if(Predecessor(succ) != node) {
				return false;
			}
		}else{
			maximal = node;
		}

		if(leftBlackDepth != rightBlackDepth) {
			return false;
		}
		blackDepth = leftBlackDepth;
		if(h(node)->color == ColorType::Black)
			blackDepth++;

		if(!A::CheckInvariant(*static_cast<D*>(this), node))
			return false;

		return true;
	}

private:
	void *root;
};

template<typename T, HookStruct T:: *Member, typename L, typename A>
struct TreeStruct : TreeCrtpStruct<TreeStruct<T, Member, L, A>, T, Member, A> {
private:
	using TreeCrtpStruct<TreeStruct<T, Member, L, A>, T, Member, A>::InsertRoot;
	using TreeCrtpStruct<TreeStruct<T, Member, L, A>, T, Member, A>::InsertLeft;
	using TreeCrtpStruct<TreeStruct<T, Member, L, A>, T, Member, A>::InsertRight;
public:
	using TreeCrtpStruct<TreeStruct<T, Member, L, A>, T, Member, A>::GetLeft;
	using TreeCrtpStruct<TreeStruct<T, Member, L, A>, T, Member, A>::GetRight;
	using TreeCrtpStruct<TreeStruct<T, Member, L, A>, T, Member, A>::GetRoot;

public:
	TreeStruct(L less = L())
	: less{sl::Move(less)} { }

public:
	void Insert(T *node) {
		if(!GetRoot()) {
			InsertRoot(node);
			return;
		}

		T *current = GetRoot();
		while(true) {
			if(less(*node, *current)) {
				if(GetLeft(current) == nullptr) {
					InsertLeft(current, node);
					return;
				}else{
					current = GetLeft(current);
				}
			}else{
				if(GetRight(current) == nullptr) {
					InsertRight(current, node);
					return;
				}else{
					current = GetRight(current);
				}
			}
		}
	}

private:
	L less;
};

template<typename T, HookStruct T:: *Member, typename A>
struct TreeOrderStruct : TreeCrtpStruct<TreeOrderStruct<T, Member, A>, T, Member, A> {
private:
	using TreeCrtpStruct<TreeOrderStruct<T, Member, A>, T, Member, A>::InsertRoot;
	using TreeCrtpStruct<TreeOrderStruct<T, Member, A>, T, Member, A>::InsertLeft;
	using TreeCrtpStruct<TreeOrderStruct<T, Member, A>, T, Member, A>::InsertRight;
public:
	using TreeCrtpStruct<TreeOrderStruct<T, Member, A>, T, Member, A>::GetLeft;
	using TreeCrtpStruct<TreeOrderStruct<T, Member, A>, T, Member, A>::GetRight;
	using TreeCrtpStruct<TreeOrderStruct<T, Member, A>, T, Member, A>::GetRoot;

public:
	void Insert(T *before, T *node) {
		if(!before) {
			T *current = GetRoot();
			if(!current) {
				InsertRoot(node);
				return;
			}

			while(GetRight(current)) {
				current = GetRight(current);
			}
			InsertRight(current, node);
		}else {
			T *current = GetLeft(before);
			if(!current) {
				InsertLeft(before, node);
				return;
			}

			while(GetRight(current)) {
				current = GetRight(current);
			}
			InsertRight(current, node);
		}
	}
};

} 

using RBTreeHook = _redblack::HookStruct;
using NullAggregator = _redblack::NullAggregator;

template<typename T, RBTreeHook T:: *Member, typename L, typename A = NullAggregator>
using RBTree = _redblack::TreeStruct<T, Member, L, A>;

template<typename T, RBTreeHook T:: *Member, typename A = NullAggregator>
using RBTreeOrder = _redblack::TreeOrderStruct<T, Member, A>;

}
