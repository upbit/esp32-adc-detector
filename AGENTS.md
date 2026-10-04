## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.


## 2. Follow the Architecture

**Find the design first. Match it exactly. Never resolve architectural ambiguity yourself..**

This rule applies to changes involving module boundaries, dependencies, interfaces, control flow, lifecycle, or state transitions.

* Locate the design or UML diagram for the affected module.
* Use the diagram that defines the relevant behavior or structure (class, sequence, state, component, etc.).
* Match the implementation to the diagram's modules, dependencies, interfaces, and control flow.
* Do not add, remove, merge, split, replace, or reroute architectural components unless the design explicitly requires it.
* If the implementation cannot match the design, stop and ask for human confirmation.
* Never make an architectural deviation without explicit approval.


## 3. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

