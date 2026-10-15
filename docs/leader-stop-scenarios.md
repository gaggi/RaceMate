# Leader-stop scenarios

The restored leader metrics expose the existing no-future-stop prediction. They do not automatically shorten the driver's fuel plan. The leader's total progress at time expiry, the leader's projected final-lap start clock and the player's rounded finishing lap are separate quantities.

## Available information

RaceMate reads the player's FuelLevel, learned consumption and restricted tank capacity. It does not have a per-car fuel-level array for competitors. CarIdx lap progress, pit state and observed pit/stint histories are available. Knowing how long a competitor's previous stint lasted does not prove a full tank, its fuel consumption, or how much it refuelled.

For the player's own car as leader, a stop requirement can be calculated from the player's fuel plan. For another car, a categorical “needs a stop” or “can finish” would invent certainty. An optional future estimate must expose its assumptions and retain an Unknown state.

The predictor already accounts for the remaining loss of a car currently in the pit lane. It learns net pit loss from observed entries and exits against that car's pace, with an explicit fallback when there is no history. This is distinct from predicting that the car will enter the pits later.

## Suggested first implementation

Provide an optional, explicitly labelled comparison such as “Assume the current leader still stops: +45 s net loss”. This should be a scenario chosen by the user, not automatic fuel-saving advice. Net pit loss means additional time compared with driving the same section normally; stationary service time or total pit-lane time alone is not equivalent.

Run the existing field model with that delay assigned to the selected current leader. Every other contender must remain in the calculation. Recompute the projected winner, race distance and the player's first eligible finish crossing, preserving multiclass lapping. Compare both cases:

- Current pace, no future stop: leader distance at expiry; winner and race finishing lap; player's finishing lap and fuel requirement.
- Assumed leader stop: the same quantities, plus the assumed delay and the identity of the car it applies to.

If the leader changes, discard or explicitly rebind the assumption; never silently apply it to a different driver. An already active pit visit must not also receive the same future-stop loss. Missing pace or incomplete field data must keep the result unknown. Historic replay must not feed live stint evidence.

The ordinary fuel requirement should continue to use the baseline until the user deliberately selects a scenario. Both alternatives should remain visible when their finishing laps differ. Do not automatically remove a lap of fuel merely because a competitor appears likely to pit.

## The 19.08-lap example

At 162.5 seconds per lap, progress 19.08 at time expiry corresponds to about 13 seconds remaining when the leader completes lap 19 and starts lap 20. Adding 45 seconds net loss brings that leader below 19 laps at expiry. With no faster contender taking over, the projected race finish can become lap 19 instead of lap 20.

However, if the next contender still reaches 19.02 laps at expiry, that car can preserve the lap-20 finish. The result cannot be inferred from the old leader alone. PredictionTests covers both an active pit loss removing that leader's extra lap and another contender retaining the longer race.

Likewise, 19.85 laps has a much larger margin: about 138 seconds at the same pace. A 45-second loss does not cross that lap boundary. This is why displaying the fraction and the final-lap clock is useful even before a future-stop model exists.

## Later inference from observed stints

An optional estimate could compare remaining race distance with the leader's current stint age and several fully observed green stints of the same car. Require an observed stint start, reject towing, incomplete visits and replay-derived samples, and show sample count and the range of observed stint lengths. Keep “not enough data” separate from “likely another stop”.

Fuel saving, partial fills, caution periods, driver changes and traffic limit this estimate. A maximum observed stint is an observation, not the car's maximum possible range. It should produce an explanatory likelihood/range, not replace the baseline with a supposedly certain shorter race.
