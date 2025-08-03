# Statistical Model Analysis: Semi-Markov Models for Human Behavior

## What Type of Statistical Model Are We Using?

### Core Model: Semi-Markov Process (SMP)

Our algorithm implements a **Semi-Markov Process**, which is a stochastic process that extends regular Markov chains by explicitly modeling the time spent in each state.

**Mathematical Definition:**
- **State Space**: S = {watching_after_scroll, watching_after_like, watching_after_dubious, scroll, like, dubious_scroll}
- **Transition Probabilities**: P(X_{n+1} = j | X_n = i) for discrete state transitions
- **Sojourn Time Distributions**: F_i(t) = P(sojourn time in state i ≤ t)

### Key Components

1. **Discrete-Time Markov Chain**: For action transitions
2. **Continuous-Time Distribution**: For dwell/sojourn times
3. **State-Dependent Parameters**: Different behavior based on previous actions

## Statistical Assumptions We're Making

### 1. Markov Property (Conditional Independence)
**Assumption**: The future behavior depends only on the current state, not on the entire history.

```
P(X_{n+1} = j | X_n = i, X_{n-1} = k, ..., X_1) = P(X_{n+1} = j | X_n = i)
```

**What this means in practice:**
- Your next action (scroll/like/dubious) only depends on what you just did
- It doesn't matter what you did 5 videos ago
- All relevant "context" is captured in the current state

**Real-world validity:**
- ✅ **Reasonable**: Recent actions heavily influence immediate behavior
- ❌ **Limitation**: Ignores longer-term patterns (mood, fatigue, content preferences)

### 2. Exponential Distribution for Dwell Times
**Assumption**: Time spent watching follows an exponential distribution within each state.

```
f(t) = λe^{-λt}  where λ = 1/mean_dwell_time
```

**What this implies:**
- **Memoryless Property**: P(T > s+t | T > s) = P(T > t)
- Most dwell times are short, but occasionally very long
- Constant "hazard rate" - probability of taking action doesn't increase with time watched

**Real-world validity:**
- ✅ **Good fit**: Many human waiting times are approximately exponential
- ✅ **Tractable**: Easy to sample and mathematically convenient
- ❌ **Oversimplified**: Real attention might have "sweet spots" or fatigue effects

### 3. Stationary Transition Probabilities
**Assumption**: The probability of transitions doesn't change over time.

```
P(scroll | watching_after_like) = constant across all sessions
```

**What this means:**
- A user's behavioral patterns are stable
- Morning TikTok usage = evening TikTok usage
- Learning/adaptation effects are ignored

**Real-world validity:**
- ✅ **Short-term reasonable**: Behavior is relatively stable within sessions
- ❌ **Long-term problematic**: People's preferences evolve, algorithms learn

### 4. Independence of Dwell Times and Next Actions
**Assumption**: How long you watch is independent of what you'll do next (given the current state).

```
P(action = scroll, dwell = 2.5s | state) = P(action = scroll | state) × P(dwell = 2.5s | state)
```

**Real-world validity:**
- ❌ **Questionable**: People who watch longer might be more likely to like
- This is a significant limitation of our current model

### 5. Discrete Action Space
**Assumption**: All user behavior can be categorized into three discrete actions.

**What we're ignoring:**
- Partial scrolls (scroll halfway then stop)
- Comments, shares, follows
- Replay behaviors
- Multi-touch gestures

### 6. Homogeneous Population
**Assumption**: All users follow the same underlying behavioral model.

**What this ignores:**
- Age differences (teens vs adults scroll differently)
- Cultural differences
- Individual personality traits
- Device differences (phone vs tablet)

## Mathematical Formulation

### State Transition Model
For our improved model with memory:

```
State space: S = {S₀, S₁, S₂} where:
- S₀ = watching_after_scroll
- S₁ = watching_after_like  
- S₂ = watching_after_dubious

Transition matrix P where P[i,j] = P(next_action = j | current_state = i):

P = [P₀₀ P₀₁ P₀₂]  [scroll→scroll   scroll→like   scroll→dubious]
    [P₁₀ P₁₁ P₁₂]  [like→scroll    like→like    like→dubious ]
    [P₂₀ P₂₁ P₂₂]  [dubious→scroll dubious→like dubious→dubious]
```

### Dwell Time Model
```
T_i ~ Exponential(λᵢ) where λᵢ = 1/μᵢ

μ₀ = mean dwell after scroll     (≈ 3.1s)
μ₁ = mean dwell after like       (≈ 0.9s)  
μ₂ = mean dwell after dubious    (≈ 5.0s)
```

### Joint Process
The complete model is:
```
X(t) = (State(t), TimeInState(t))

Where State(t) follows the Markov chain and TimeInState(t) follows 
state-dependent exponential distributions.
```

## Model Limitations and Assumptions We Should Question

### 1. Exponential Assumption Issues
**Problem**: Real attention spans might have:
- **Minimum attention time**: People rarely scroll in < 0.5 seconds
- **Maximum attention time**: Very few people watch > 30 seconds
- **Bi-modal distributions**: Quick scrolls vs engaged watching

**Better alternatives**: 
- Gamma distribution (more realistic shape)
- Mixture models (quick vs engaged modes)
- Truncated distributions

### 2. Missing Covariates
**What we're not modeling**:
- Video content features (music, faces, trending topics)
- Time of day effects
- User fatigue within session
- Social context (alone vs with friends)

### 3. Aggregation Bias
**Problem**: We assume one "average user" but real populations have:
- **Heavy users** vs **casual users**
- **Content creators** vs **pure consumers**  
- **Different age groups** with distinct patterns

### 4. Feedback Loops
**Missing**: The recommendation algorithm learns from user behavior, creating:
- **Personalization effects**: Better content → longer watch times
- **Filter bubbles**: Reduced content diversity over time
- **Engagement optimization**: Algorithm pushes toward addictive patterns

## When These Assumptions Break Down

### Short-term (minutes): ✅ Model works well
- Markov property reasonable
- Exponential times approximately correct
- Transition probabilities stable

### Medium-term (hours): ⚠️ Model starts to degrade
- User fatigue effects
- Time-of-day variations
- Content saturation

### Long-term (days/weeks): ❌ Model breaks down
- Preference evolution
- Seasonal effects
- Platform algorithm changes
- Social trend shifts

## Alternative Modeling Approaches

### 1. Hidden Markov Models (HMM)
- Model unobserved "engagement states" (bored, interested, addicted)
- Observable actions depend on hidden psychological state

### 2. Point Processes
- Model action times as a continuous-time point process
- Can capture acceleration/deceleration patterns

### 3. Mixture Models
- Different user types with different behavioral patterns
- Could capture heavy users vs casual browsers

### 4. Reinforcement Learning Models
- Model user as agent learning which content types to engage with
- Captures adaptation and preference evolution

## Conclusion

Our semi-Markov model makes strong but reasonable assumptions for **short-term behavioral simulation**. It's excellent for:
- Testing recommendation algorithms
- Generating realistic usage data
- Understanding immediate behavioral patterns

However, it oversimplifies:
- Long-term behavioral evolution
- Individual differences
- Complex psychological states
- Algorithm-user feedback loops

The model is a useful **first approximation** but shouldn't be mistaken for a complete theory of human social media behavior.
