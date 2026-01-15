---
name: User story
about: Converts your idea into an actionable format ready for sprint implementation.
title: ''
labels: Type:Story
assignees: ''

---

# Description

## User Stories

- ***As a ..., I want to ... so that ... (please stick to who, what, why)***

## Value
- 

## Acceptance Criteria
- 

## Definition of ready
- [ ] Everybody needs to understand the value written in the user story
- [ ] Acceptance criteria have to be defined
- [ ] All dependencies of the user story need to be identified
- [ ] Feature should be seen from an end user perspective
- [ ] Story has to be estimated
- [ ] Story points need to be less than 20

## Definition of done
- Functional requirements
  - [ ] Functionality described in the user story works
  - [ ] Acceptance criteria are fulfilled
- Quality
  - [ ] Code review happened
  - [ ] CI is green (that includes new and existing automated tests)
  - [ ] Critical code received unit tests by the developer
- Non-functional requirements
  - [ ] No sonar cloud issues
- Configuration changes
  - [ ] The next branch of the OpenCloud charts is compatible
 

<details>

<summary>Writing Tips</summary>

## User Story
INVEST Criteria for User Stories

- **Independent**  
  Should be self-contained in a way that allows being released **without depending on one another**.

- **Negotiable**  
  Only **capture the essence** of the user's need, leaving room for conversation. A user story should not be written like a contract.

- **Valuable**  
  Delivers value to the end user.

- **Estimable**  
  User stories must be estimated so they can be properly prioritized and fit into sprints.

- **Small**  
  A user story is a small chunk of work that allows it to be completed in a short period of time.

- **Testable**  
  A user story has to be confirmed via pre-written acceptance criteria.

## Value
Examples:
- Save time
- Reduce risk
- Make it accessible for anyone

## Acceptance Criteria

### What Acceptance Criteria are for
Acceptance Criteria answer one question only:

**How do we know this story is done?**

Not how it is implemented. Not what might be nice. Not future hypotheticals.


#### Tie every AC to user value
Each criterion must protect or enable the user benefit.

**Bad**
- API returns 200 OK

**Good**
- User sees a confirmation that the action succeeded

If the user would not notice a failure, question why the AC exists.

#### Use observable outcomes
ACs must be verifiable by anyone, not just engineers.

**Bad**
- System processes data efficiently

**Good**
- Results are shown within 2 seconds after submission

If you cannot test it without reading code, it is trash.

#### Write from the user’s perspective
Describe what the user can do or see, not internal behavior.

**Bad**
- Data is stored in the new table

**Good**
- User can see previously saved entries after reload

#### Keep ACs binary
Each AC should be clearly pass or fail.

**Bad**
- Works well on mobile

**Good**
- User can complete the flow on a mobile device without horizontal scrolling

If there is room for interpretation, it will be abused.

#### Cover the happy path first
Do not drown the story in edge cases.

Start with:
- Core flow
- Primary user goal

Add edge cases only if they:
- Prevent real harm
- Block release
- Create user-visible failure

### Avoid solutioning
ACs define what, not how.

**Bad**
- Button is implemented using component X

**Good**
- User can submit the form using a visible primary action

If you lock implementation, you kill collaboration.

#### Use Given / When / Then where helpful
Optional, but useful for clarity.

**Example**
- **Given** the user is logged in  
- **When** they submit the form  
- **Then** they see a success message and the data is saved

If it adds noise, skip it.


### Litmus test
A good set of ACs allows:
- A developer to build it
- A tester to verify it
- A product manager to accept or reject it

Without further clarification.

If not, rewrite.

</details>
