#pragma once

#include <stddef.h>

namespace kernel::adt {

template<typename T>
struct AvlNode {
    T *left = nullptr;
    T *right = nullptr;
    T *parent = nullptr;
    int height = 1;
};

template<typename T, AvlNode<T> T::*NodeMember>
class AvlTree {
    public:
    T *root = nullptr;

    constexpr AvlTree() noexcept = default;

    void insert(T *node) noexcept {
        if (!root) {
            root = node;
            (node->*NodeMember).parent = nullptr;
            (node->*NodeMember).left = nullptr;
            (node->*NodeMember).right = nullptr;
            (node->*NodeMember).height = 1;
            return;
        }

        T *current = root;
        T *parent = nullptr;

        while (current) {
            parent = current;
            if (*node < *current) {
                current = (current->*NodeMember).left;
            } else {
                current = (current->*NodeMember).right;
            }
        }

        (node->*NodeMember).parent = parent;
        (node->*NodeMember).left = nullptr;
        (node->*NodeMember).right = nullptr;
        (node->*NodeMember).height = 1;

        if (*node < *parent) {
            (parent->*NodeMember).left = node;
        } else {
            (parent->*NodeMember).right = node;
        }

        rebalance(parent);
    }

    void remove(T *node) noexcept {
        if (!node) return;

        T *parent = (node->*NodeMember).parent;
        T *rebalance_start = parent;

        if (!(node->*NodeMember).left || !(node->*NodeMember).right) {
            T *child = (node->*NodeMember).left ? (node->*NodeMember).left : (node->*NodeMember).right;
            replace_node(node, child);
        } else {
            T *successor = find_min((node->*NodeMember).right);
            rebalance_start = (successor->*NodeMember).parent;
            if (rebalance_start == node) rebalance_start = successor;

            if ((successor->*NodeMember).parent != node) {
                replace_node(successor, (successor->*NodeMember).right);
                (successor->*NodeMember).right = (node->*NodeMember).right;
                if ((successor->*NodeMember).right) {
                    ((successor->*NodeMember).right->*NodeMember).parent = successor;
                }
            }

            replace_node(node, successor);
            (successor->*NodeMember).left = (node->*NodeMember).left;
            if ((successor->*NodeMember).left) {
                ((successor->*NodeMember).left->*NodeMember).parent = successor;
            }
            update_height(successor);
        }

        if (rebalance_start) rebalance(rebalance_start);
    }

    template<typename K>
    T *find(const K &key) const noexcept {
        T *current = root;
        while (current) {
            if (key == *current) return current;
            if (key < *current) current = (current->*NodeMember).left;
            else current = (current->*NodeMember).right;
        }
        return nullptr;
    }

    private:
    static int get_height(T *node) noexcept {
        return node ? (node->*NodeMember).height : 0;
    }

    static void update_height(T *node) noexcept {
        if (!node) return;
        int hl = get_height((node->*NodeMember).left);
        int hr = get_height((node->*NodeMember).right);
        (node->*NodeMember).height = (hl > hr ? hl : hr) + 1;
    }

    static int get_balance(T *node) noexcept {
        if (!node) return 0;
        return get_height((node->*NodeMember).left) - get_height((node->*NodeMember).right);
    }

    void replace_node(T *old_node, T *new_node) noexcept {
        T *parent = (old_node->*NodeMember).parent;
        if (!parent) {
            root = new_node;
        } else if ((parent->*NodeMember).left == old_node) {
            (parent->*NodeMember).left = new_node;
        } else {
            (parent->*NodeMember).right = new_node;
        }
        if (new_node) {
            (new_node->*NodeMember).parent = parent;
        }
    }

    T *rotate_right(T *y) noexcept {
        T *x = (y->*NodeMember).left;
        T *T2 = (x->*NodeMember).right;

        replace_node(y, x);
        (x->*NodeMember).right = y;
        (y->*NodeMember).parent = x;
        (y->*NodeMember).left = T2;
        if (T2) (T2->*NodeMember).parent = y;

        update_height(y);
        update_height(x);
        return x;
    }

    T *rotate_left(T *x) noexcept {
        T *y = (x->*NodeMember).right;
        T *T2 = (y->*NodeMember).left;

        replace_node(x, y);
        (y->*NodeMember).left = x;
        (x->*NodeMember).parent = y;
        (x->*NodeMember).right = T2;
        if (T2) (T2->*NodeMember).parent = x;

        update_height(x);
        update_height(y);
        return y;
    }

    void rebalance(T *node) noexcept {
        while (node) {
            update_height(node);
            int balance = get_balance(node);

            if (balance > 1) {
                if (get_balance((node->*NodeMember).left) < 0) {
                    rotate_left((node->*NodeMember).left);
                }
                node = rotate_right(node);
            } else if (balance < -1) {
                if (get_balance((node->*NodeMember).right) > 0) {
                    rotate_right((node->*NodeMember).right);
                }
                node = rotate_left(node);
            }
            node = (node->*NodeMember).parent;
        }
    }

    static T *find_min(T *node) noexcept {
        while (node && (node->*NodeMember).left) {
            node = (node->*NodeMember).left;
        }
        return node;
    }
};

} // namespace kernel::adt
