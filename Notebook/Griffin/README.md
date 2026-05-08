![Final Product](../Images/IMG_7206.png)


![In The Workshop](../Images/IMG_9645.png)

Febuary 4th

Objective: Figure out the physical design of what we want to make, and whether we want to use the machine shop.
Record of what was done: We discussed the physical design for a while. We discussed different ways of making a lazy susan design. Options include rotating the spice dispenser using ball berrings and a central motor, or rotating the cup to be dispensed to to the spice dispensers themselves. Pros of rotating the spice containers include increased visual appeal, less total motors, and simpler fittings for aligning spices (only need one funnel/dispensing shoot). Pros of rotating the cup include ease of design and less moving parts.

Febuary 5th

Objective: Meet with Greg/Machine Shop about ideas.
Record of what was done: Greg brought up the point that we really do not need to rotate anything. Everything can just be dispensed from their own containers into a central funnel. The downside of this is each spice container needs its own motor, increasing cost. Therefore, we settled on just 3 spices as essentially a proof of concept. Scaling does not increase the design complexity much. Can be made as soon as we supply motors.

Febuary 9th

Objective: Discuss additional hardware complexity.
Record of what was done: Settled on adding IR sensor for spice level detection. Two options for this. One, smoke detector style break beam detector. Two, IR time of flight sensor. Decided on IR Time of Flight for higher precision vs the digital high/low supplied by break beam style. Drew this image to explain how it worked to my groupmates.

![In The Workshop](../Images/79236442504__7CD3825E-1CEB-405F-BD5A-CF5BF4BBF05C.HEIC)

Febuary 11th

Objective: Put in myece order request for load cells and motors.
Record: Ordered these parts from Amazon through my.ece
Load Cell
https://www.amazon.com/Wishiot-Precision-Miniature-Electronic-Portable/dp/B0C3QHT4TT/ref=sr_1_18?crid=YHTPU8K7PRR0&dib=eyJ2IjoiMSJ9.ZtfBVBQNOxQfS2RLniF4zJVTIeswGS6WmkaQ2EmqnAWr1R8vger4_pDiZ2euk6cIWGjregdFvOeWiDsReAurS0UYXRainXTYUk-QG1Kar69X53pMdBWGlf-Tr17roMO7grSCU-BvZ6UhLg4trPYlvaWqRR4G1spqeQVUTUfz06yyokeWtq6mjGF5unZnc15sofcRdEXps-s46nm1j4gdzahU57k0v8zQwIDBRcPwfDg.NsLVxuNE5ZvPOyESLpK9di_2pKtzg1X5-PoH1h3RUZ4&dib_tag=se&keywords=load+cell&qid=1778208926&sprefix=load+cel%2Caps%2C382&sr=8-18

Stepper Motors
https://www.amazon.com/STEPPERONLINE-Stepper-Bipolar-42x42x38mm-Connector/dp/B0B38GHRH8/ref=sr_1_5?crid=MPZIC8GPXWF8&dib=eyJ2IjoiMSJ9.hN-9QQUUabt-Xybqh_2heZ1beHCjbW0sVhGOsdmrCYtF9pC2RRg_t21PWD54Q4zSNgISEJ2SfiFH74Rl3Dc9I_2hlNBQUAqaO30FQkyQqsgNyQwXgb5fuh_wm3C2hRaMf7h5kmbTRDegiK256D2SiX6b6eJhIHbYGNB9iyCJbpnkGYnJT4oeXz8MGXJzHr7PQgqlB78Lz1JtoUl-8t0J3wMspDLAo3t2IQyZDh6o3Zs.7PPbeHkdu_9rmu1kFfhMCw_SWu9VS72QoQEWxrQEN6M&dib_tag=se&keywords=nema17%2Bstepper%2Bmotor&qid=1778209016&sprefix=nema17%2Bstepper%2Bmotor%2Caps%2C161&sr=8-5&th=1

Load cell is a 1kg load cell. Plenty large enough for the amount of spice we intend to measure, along with most smaller ingredients, while still providing enough precision for smaller amounts of strong spices.
Stepper motors are 1.5A, which on on edge of typical rated discharge current for lithium ion batteries. Will need to be careful in designing around this and to only run one motor at a time.

Febuary 17th

Objective: Take delivery of parts
Record: Took delivery of load cell and stepper motors. Delivered to Machine Shop. First order put in and should be done by end of week or early next week.

Febuary 20th

Objective: Start PCB schematic work.
Record of what was done: Have past PCB design experience, able to copy over microcontroller schematic, usbc port schematic, and voltage regulation schematic from prior project. Will need to migrate ESP32 C3-Mini to ESP32 S3-Wroom for more GPIO pins though. Attached are the copied schematics.

