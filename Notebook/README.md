# General Information about our ECE445 NuChef AI Culinary Assistant

## Problem

The processed food industry has become increasingly toxic due to chemical flavor additives, which have been associated with a 12%–32% higher cancer risk. At the same time, getting into cooking can be intimidating for new chefs. As a result, students and working professionals may rely on convenient but unhealthy meals.

Most currently available smart-cooking tools do not provide a real-time experience that guides users through the complete process from raw ingredients to the finished dish. This makes it difficult for users to learn how to cook or even get started. It is also inefficient to design recipes that adapt to the ingredients the user already has available, since a prospective cook would otherwise need to manually catalog everything on hand.

This creates two main problems: users may waste food, and recipe creation remains an expert skill that is difficult to customize. Spices are especially difficult to measure and control in a dish, even though they are often the most important contributors to flavor. A healthy and delicious diet is important for productivity and long-term health, but it can be difficult to accomplish consistently.

## Solution

We propose an **AI-Nutritious Culinary Assistant** that recognizes available ingredients and generates a personalized recipe with interactive, step-by-step guidance.

The system uses the **Meta Quest 3** as the user interface and sensor front-end. The headset streams video and voice commands to an edge vision processor running an ingredient-recognition pipeline. In addition to vision, the device integrates an environmental sensor module that measures ingredient weight for portion verification.

The appliance also includes a circular seasoning dispenser driven by stepper motors for proportional seasoning action. This enables closed-loop “dispense to target grams” assistance during cooking. The spice containers also contain IR sensors, allowing users to view the percentage of each spice remaining and receive an alert when a spice is running low.

## Visual Aid

**Figure 1. High-Level Abstraction of the Project Pipeline**

![High Level Abstraction of The Project Pipeline](<Visual Aid.png>)