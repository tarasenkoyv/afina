#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <cstring>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    volatile char stack_addr;
    if (_stack_direction) {
        ctx.Low = const_cast<char*>(&stack_addr);
    } 
    else {
        ctx.Hight = const_cast<char*>(&stack_addr);
    }

    std::size_t stack_size = ctx.Hight - ctx.Low;
    if (std::get<1>(ctx.Stack) < stack_size || std::get<1>(ctx.Stack) > stack_size * 2) {
        delete[] std::get<0>(ctx.Stack);
        std::get<0>(ctx.Stack) = new char[stack_size];
        std::get<1>(ctx.Stack) = stack_size;
    }
    
    std::memcpy(std::get<0>(ctx.Stack), ctx.Low, stack_size);
}

void Engine::Restore(context &ctx) {
    volatile char stack_addr;
    bool call_restore = _stack_direction ? (&stack_addr >= ctx.Low) : (&stack_addr <= ctx.Hight);
    if (call_restore) {
        Restore(ctx);
    }
    std::memcpy(ctx.Low, std::get<0>(ctx.Stack), ctx.Hight - ctx.Low);
    cur_routine = &ctx;
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    context* coroutine = alive;
    if (coroutine != nullptr)
    {
        if (coroutine == cur_routine)
        {
            coroutine = coroutine->next;
        }
    }
    if (coroutine != nullptr)
    {
        sched(coroutine);
    }
}

void Engine::sched(void *routine_) {
    if (routine_ == nullptr) {
        yield();
        return;
    }

    context* sched_cur = static_cast<context*>(routine_);
    if (sched_cur == cur_routine || sched_cur->is_blocked) {
        return;
    }
    
    if (cur_routine != idle_ctx) {
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
        Store(*cur_routine);
    }
    cur_routine = sched_cur;
    Restore(*cur_routine);
}

void Engine::delete_from_list(context*& list, context*& routine_) {
    if (list == nullptr) {
        return;
    }
    if (list == routine_) {
        list = routine_->next;
    }
    if (routine_->next != nullptr) {
        routine_->next->prev = routine_->prev;
    }
    if (routine_->prev != nullptr) {
        routine_->prev->next = routine_->next;
    }
}

void Engine::add_to_list(context*& list, context*& routine_) {
    if (list == nullptr) {
        list = routine_;
        list->next = nullptr;
        list->prev = nullptr;
    } 
    else {
        routine_->next = list;
        routine_->prev = nullptr;
        list->prev = routine_;
        list = routine_;
    }
}

void Engine::block(void *routine_) {
    if (routine_ == nullptr || cur_routine == routine_) {
        delete_from_list(alive, cur_routine);
        add_to_list(blocked, cur_routine);
        cur_routine->is_blocked = true;
        sched(idle_ctx);
    } 
    else {
        context* coro = static_cast<context*>(routine_);
        delete_from_list(alive, coro);
        add_to_list(blocked, coro);
        coro->is_blocked = true;
    }
}

void Engine::unblock(void *routine_) {
    if (routine_ == nullptr) {
        return;
    }
    context* coro = static_cast<context*>(routine_);
    delete_from_list(blocked, coro);
    add_to_list(alive, coro);
    coro->is_blocked = false;
}

} // namespace Coroutine
} // namespace Afina
