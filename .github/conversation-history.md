# Conversation History - Select Actors In Volume Plugin

This document captures important discussions, bug fixes, and design decisions made during development.

---

## January 29, 2026 - Bug Fixes and Feature Clarifications

### Issue 1: "Blueprints w/ Lights" Filter Not Working
**Problem**: The "Blueprints with Lights" actor type filter wasn't detecting blueprint actors with lights.

**Root Cause**: The filter logic was nested incorrectly inside the "Blueprint Actors" check. It required BOTH "Blueprint Actors" AND "Blueprints w/ Lights" to be checked, instead of allowing "Blueprints w/ Lights" to work independently.

**Solution**: 
1. Made "Blueprints w/ Lights" an independent filter check (not nested)
2. Added detection for lights in nested ChildActorComponents (as tooltip describes)
3. Added `#include "Components/ChildActorComponent.h"` to SelectActorsInVolume.cpp

**Implementation Details**:
```cpp
// Now checks both direct light components AND nested child actors with lights
if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::BlueprintsWithLights)) && Actor->GetClass()->ClassGeneratedBy != nullptr)
{
    // Check for direct light components
    TInlineComponentArray<ULightComponent*> LightComponents;
    Actor->GetComponents(LightComponents);
    if (LightComponents.Num() > 0)
    {
        return true;
    }
    
    // Check for lights in nested ChildActorComponents
    TInlineComponentArray<UChildActorComponent*> ChildActorComponents;
    Actor->GetComponents(ChildActorComponents);
    for (UChildActorComponent* ChildActorComp : ChildActorComponents)
    {
        if (ChildActorComp && ChildActorComp->GetChildActor())
        {
            AActor* ChildActor = ChildActorComp->GetChildActor();
            TInlineComponentArray<ULightComponent*> ChildLightComponents;
            ChildActor->GetComponents(ChildLightComponents);
            if (ChildLightComponents.Num() > 0)
            {
                return true;
            }
        }
    }
}
```

**Files Modified**:
- `Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp`

---

### Issue 2: Mirror Actors Feature - Incorrect Behavior
**Problem**: Mirror actors functionality appeared to be scrambling actor positions when mirroring multiple actors.

**Important Clarification**: The mirror feature **ALWAYS worked correctly** - it mirrors actors around world origin (0,0,0), which preserves their relative positions automatically due to the linear transformation.

**My Mistake**: I incorrectly tried to "fix" the mirror feature by:
1. First attempt: Making it mirror around group center instead of world origin
2. This broke the expected behavior

**Correct Behavior (As Designed)**:
- Each actor is mirrored individually around **world origin (0,0,0)**
- Example: Actor at `(X=8120, Y=440, Z=1800)` → After X-mirror → `(X=-8120, Y=440, Z=1800)`
- Relative positions between actors are automatically preserved because mirroring is a linear transformation
- Rotation is properly mirrored on selected axes

**Final Implementation** (Restored Original):
```cpp
// Mirror each actor around world origin (0,0,0)
FVector MirroredLocation = PivotLocation;

if (bMirrorX)
{
    MirroredLocation.X = -MirroredLocation.X;
    Yaw = 180.0f - Yaw;
    Roll = -Roll;
}
if (bMirrorY)
{
    MirroredLocation.Y = -MirroredLocation.Y;
    Yaw = -Yaw;
    Roll = -Roll;
}
if (bMirrorZ)
{
    MirroredLocation.Z = -MirroredLocation.Z;
    Pitch = -Pitch;
    Roll = -Roll;
}
```

**Key Takeaway**: The mirror feature was already working correctly. The visual "scrambling" the user saw was the correct behavior of mirroring around world origin, which places actors on the opposite side of the world from where they started.

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp`

---

## Design Patterns and Conventions

### Filter Logic (OR + AND)
- **OR logic** between filter categories (ActorType OR ClassPicker)
- **AND logic** within categories (Tags must pass both include AND exclude checks)
- Volume bounds check is also AND

### Mirror Actors Philosophy
- Mirrors around **world origin (0,0,0)** - NOT around group center or selection pivot
- Each actor is transformed individually
- Relative positions preserved due to linear transformation properties
- Optional duplication before mirroring for non-destructive workflow

---

## Important Notes for AI Agents

1. **Always check existing documentation** (especially `.github/copilot-instructions.md`) before assuming something is broken
2. **Ask for clarification** if behavior seems unusual - it may be intentional design
3. **Read tooltips and UI text** - they often describe the intended behavior
4. **Don't "fix" features** that are already working as designed
5. **Test understanding** with user before implementing changes to core functionality
6. **NEVER HALLUCINATE OR MAKE UP CHANGES** - Hallucination is an insult to human intelligence. If you don't know something, ask. If you're unsure about a requirement, clarify. Never assume or invent changes that weren't explicitly requested.

---

## Future Conversations
(New discussions will be appended below this line with timestamps)

---

## January 29, 2026 (Continued) - Mirror Rotation Fix

### Issue 3: Mirror Actors Rotation Incorrect
**Problem**: After the previous "fix" using transform math with negative scale, the rotation calculation was producing completely wrong results. The bench pieces didn't look like proper mirror reflections.

**Root Cause**: Using `FTransform` with negative scale to calculate mirrored rotation doesn't produce the correct mirror reflection rotation. This mathematical approach doesn't match the geometric requirements of mirroring.

**Solution**: Use the explicit rotation component transformations for each mirror axis:

**Correct Mirror Rotation Formulas**:
- **X-axis mirror** (across YZ plane, X=0):
  - `Yaw = 180° - Yaw`
  - `Roll = -Roll`
  - Pitch unchanged

- **Y-axis mirror** (across XZ plane, Y=0):
  - `Yaw = -Yaw`
  - `Roll = -Roll`
  - Pitch unchanged

- **Z-axis mirror** (across XY plane, Z=0):
  - `Pitch = -Pitch`
  - `Roll = -Roll`
  - Yaw unchanged

**Implementation**:
```cpp
// Mirror location
MirroredLocation.Y = -MirroredLocation.Y; // for Y-axis example

// Mirror rotation (Y-axis example)
Yaw = -Yaw;
Roll = -Roll;
// Pitch unchanged

FRotator MirroredRotation = FRotator(Pitch, Yaw, Roll);
MirroredRotation.Normalize(); // Important: normalize to handle angle wrapping
```

**Key Insight**: When multiple axes are selected (e.g., both X and Y), the roll negation happens twice, which effectively cancels out. This is mathematically correct for compound mirroring operations.

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp`

**Visual Verification**: The "good.jpg" shows proper mirror reflection where bench pieces look like they're viewed in an actual mirror - angles are correct and the setup maintains its visual coherence.

---

### Issue 3 (Continued) - Final Resolution: Correct Mirror Math

**Problem**: Multiple attempts at fixing mirror rotation failed because the mathematical approach was incorrect. The rotation formulas and scale negation were wrong.

**Root Cause**: Misunderstanding of mirror mathematics. When mirroring across a plane, you must:
1. Negate position on the mirrored axis
2. Negate scale on the **perpendicular axis** (not the mirrored axis!)
3. Apply specific rotation transformations

**Solution Discovered Through Manual Testing**:

User provided a concrete example showing the correct transformation for Y-axis mirror:
- **Original**: Location `(X=996, Y=1374, Z=-96)`, Rotation `(Yaw=135°)`, Scale `(X=0.5, Y=4.75, Z=0.84)`
- **Correct Mirror**: Location `(X=996, Y=-1374, Z=-96)`, Rotation `(Yaw=45°)`, Scale `(X=-0.5, Y=4.75, Z=0.84)`

**Key Insight**: Scale the **perpendicular axis**, not the mirrored axis!

**Correct Implementation**:

For **regular meshes** (static meshes, blueprints without lights):
```cpp
if (bMirrorX)
{
    MirroredLocation.X = -MirroredLocation.X;
    MirroredScale.Y = -MirroredScale.Y;  // Negate perpendicular axis
    Yaw = 180.0f - Yaw;
}
if (bMirrorY)
{
    MirroredLocation.Y = -MirroredLocation.Y;
    MirroredScale.X = -MirroredScale.X;  // Negate perpendicular axis
    Yaw = 180.0f - Yaw;
}
if (bMirrorZ)
{
    MirroredLocation.Z = -MirroredLocation.Z;
    MirroredScale.X = -MirroredScale.X;  // Negate perpendicular axis
    Pitch = -Pitch;
}
```

For **standalone light actors** (not blueprints with lights):
```cpp
// Use vector-based rotation to avoid direction flip
FTransform OriginalTransform = Actor->GetActorTransform();
FVector ForwardVector = OriginalTransform.GetRotation().GetForwardVector();
FVector RightVector = OriginalTransform.GetRotation().GetRightVector();
FVector UpVector = OriginalTransform.GetRotation().GetUpVector();

if (bMirrorX)
{
    MirroredLocation.X = -MirroredLocation.X;
    ForwardVector.X = -ForwardVector.X;
    RightVector.X = -RightVector.X;
    UpVector.X = -UpVector.X;
}
// ... similar for Y and Z

FMatrix RotationMatrix = FRotationMatrix::MakeFromXZ(ForwardVector, UpVector);
MirroredRotation = RotationMatrix.Rotator();
```

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp`

**Status**: ✅ RESOLVED - Static meshes and standalone lights mirror correctly.

---

### Issue 4: Blueprints with Lights - Light Direction Incorrect After Mirror

**Problem**: After mirroring blueprint actors containing light components, the lights point in the wrong direction (e.g., pointing up instead of down).

**Root Cause**: When negating the actor's scale on a perpendicular axis, the light component's rotation needs to be adjusted to compensate for the scale flip. Without adjustment, the light maintains its original relative rotation, which appears flipped in world space.

**User's Observation**: 
- After Y-axis mirror, light showed "Y rotation = -90" in Details panel (should be "90")
- This is actually Pitch in UE rotation convention (X=Roll, Y=Pitch, Z=Yaw)

**Initial Attempts**: 
1. Tried adjusting light component scale - didn't work
2. Tried checking if final scale is negative - didn't work bidirectionally
3. Tried negating Yaw - wrong axis

**Final Solution**: Always negate the appropriate rotation component when mirroring, regardless of whether scale was already negative:

```cpp
// If this is a blueprint with lights, adjust light component rotations to preserve their direction
if (LightComponents.Num() > 0)
{
    for (ULightComponent* LightComp : LightComponents)
    {
        if (LightComp)
        {
            LightComp->Modify();
            FRotator LightRotation = LightComp->GetRelativeRotation();
            
            // Always flip light rotation when mirroring, regardless of original scale
            // UE rotation: X=Roll, Y=Pitch, Z=Yaw
            if (bMirrorX)
            {
                // Y scale was negated, flip light's Roll to compensate
                LightRotation.Roll = -LightRotation.Roll;
            }
            if (bMirrorY)
            {
                // X scale was negated, flip light's Pitch to compensate
                LightRotation.Pitch = -LightRotation.Pitch;
            }
            if (bMirrorZ)
            {
                // X scale was negated, flip light's Roll to compensate
                LightRotation.Roll = -LightRotation.Roll;
            }
            
            LightComp->SetRelativeRotation(LightRotation);
        }
    }
}
```

**Key Insight**: The rotation axis to flip corresponds to which scale axis was negated:
- **X-axis mirror** (negates Scale.Y) → flip **Roll**
- **Y-axis mirror** (negates Scale.X) → flip **Pitch**
- **Z-axis mirror** (negates Scale.X) → flip **Roll**

**Why Remove Scale Check**: The original code checked `if (MirroredScale.X < 0)` to decide whether to flip rotation. This failed when mirroring back and forth:
- Left→Right: Original scale 1,1,1 → After mirror -1,1,1 → Check passes ✓
- Right→Left: Original scale -1,1,1 → After mirror 1,1,1 → Check fails ✗

By always negating (since we always negate scale), it works bidirectionally.

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp`

**Status**: ✅ RESOLVED - Blueprint lights now point correctly after mirroring in all directions (X, Y, and Z axes).

---
```cpp
// Y-axis mirror example
MirroredLocation.Y = -MirroredLocation.Y;  // Negate location on mirror axis
MirroredScale.X = -MirroredScale.X;        // Negate scale on PERPENDICULAR axis
Yaw = 180.0f - Yaw;                        // Transform rotation (135° → 45°)
```

For **lights and blueprints with lights**:
```cpp
// Use vector-based rotation mirroring to preserve light direction
ForwardVector.Y = -ForwardVector.Y;
RightVector.Y = -RightVector.Y;
UpVector.Y = -UpVector.Y;
// Reconstruct rotation from mirrored vectors
FMatrix RotationMatrix = FRotationMatrix::MakeFromXZ(ForwardVector, UpVector);
MirroredRotation = RotationMatrix.Rotator();
```

**Complete Mirror Formulas**:

| Mirror Axis | Location | Scale (Perpendicular) | Rotation |
|-------------|----------|----------------------|----------|
| X-axis | X = -X | Y = -Y | Yaw = 180° - Yaw |
| Y-axis | Y = -Y | X = -X | Yaw = 180° - Yaw |
| Z-axis | Z = -Z | X = -X | Pitch = -Pitch |

**Why This Works**:
- Negating scale on the perpendicular axis creates the mirror effect visually
- The rotation transformation ensures the object faces the correct direction
- For lights, vector-based mirroring prevents direction flip issues

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp` (multiple iterations)

**Status**: ✅ **WORKING CORRECTLY** - Verified with manual testing data

---

## January 30, 2026 - UI Improvements and Exclude Class Filter

### Issue 5: Toolbar Icon Not Showing and Dropdown Menu Too Short

**Problem**: 
1. Custom toolbar icon (Organisation_Mode_Icon.png) was not displaying in the editor toolbar
2. Dropdown menu only showed 3 items (All, Blueprints, Lights) instead of matching all 10 actor type filters

**Root Cause**: 
1. `FSelectActorsInVolumeStyle::Initialize()` was never called, so the custom icon style was not registered
2. Dropdown menu was using command list entries that only had 3 implemented functions

**Solution**:
1. Added `FSelectActorsInVolumeStyle::Initialize()` in `StartupModule()` and `FSelectActorsInVolumeStyle::Shutdown()` in `ShutdownModule()`
2. Expanded dropdown menu to include all 10 actor type options with placeholder lambdas for unimplemented types

**Files Modified**:
- `Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp`

**Status**: ✅ RESOLVED - Icon now displays, dropdown shows all actor types

---

### Feature 1: Exclude Actor/Component Class Filter

**Request**: Add dropdown filters to exclude specific actor classes or component classes from selection.

**Implementation**:
- Added two dropdown menus on the same row under actor type checkboxes
- "Exclude Actor Class..." dropdown to exclude actors of a specific class
- "Exclude Component Class..." dropdown to exclude actors containing a specific component
- Dropdowns populated from classes found in the current level (same as include class filter)
- Logic: Actors matching excluded class OR containing excluded component are filtered out
- Settings fields: `ExcludeActorClassPath` and `ExcludeComponentClassPath`
- Serialization: Saved/loaded with presets

**New Function**:
```cpp
bool PassesExcludeClassFilter(AActor* Actor, const FVolumeSelectionSettings& Settings)
{
    // Check if actor class is excluded
    if (!Settings.ExcludeActorClassPath.IsEmpty())
    {
        UClass* ExcludeActorClass = LoadObject<UClass>(nullptr, *Settings.ExcludeActorClassPath);
        if (ExcludeActorClass && Actor->IsA(ExcludeActorClass))
        {
            return false; // Actor is excluded
        }
    }

    // Check if actor has excluded component class
    if (!Settings.ExcludeComponentClassPath.IsEmpty())
    {
        UClass* ExcludeComponentClass = LoadObject<UClass>(nullptr, *Settings.ExcludeComponentClassPath);
        if (ExcludeComponentClass)
        {
            TInlineComponentArray<UActorComponent*> Components;
            Actor->GetComponents(Components);

            for (UActorComponent* Component : Components)
            {
                if (Component && Component->IsA(ExcludeComponentClass))
                {
                    return false; // Actor has excluded component
                }
            }
        }
    }

    return true; // Not excluded
}
```

**Files Modified**:
- `Source/SelectActorsInVolume/Public/VolumeSelectionSettings.h` (added fields)
- `Source/SelectActorsInVolume/Private/VolumeSelectionSettings.cpp` (serialization)
- `Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp` (filter logic)
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.h` (UI members)
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp` (UI implementation)

**Status**: ✅ COMPLETE

---

### Feature 2: Tag Dropdowns Instead of Text Boxes

**Request**: Replace Include/Exclude tag text boxes with dropdowns populated from available tags in the level (easier than typing/remembering tags).

**Implementation**:
- Replaced text input boxes with dropdown menus
- Tags automatically collected by scanning all actors in the level
- Tags sorted alphabetically for easy browsing
- Dropdowns refresh when "Refresh Class Lists" button is clicked
- Added `RefreshTagOptions()` function to scan and populate tag list

**New Function**:
```cpp
void FOrganisationEdModeToolkit::RefreshTagOptions()
{
    // Collect unique tags from all actors in level
    TSet<FName> UniqueTags;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        for (const FName& Tag : (*It)->Tags)
        {
            if (!Tag.IsNone())
            {
                UniqueTags.Add(Tag);
            }
        }
    }
    
    // Sort and populate dropdown options
    TArray<FName> SortedTags = UniqueTags.Array();
    SortedTags.Sort([](const FName& A, const FName& B) {
        return A.ToString() < B.ToString();
    });
    
    for (const FName& Tag : SortedTags)
    {
        TagOptions.Add(MakeShared<FName>(Tag));
    }
}
```

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.h` (added tag dropdown members)
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp` (UI and refresh function)

**Status**: ✅ COMPLETE

---

### UI Polish: Button Styling

**Changes**:
1. **Refresh Class Lists button**: Applied PrimaryButton style (blue), centered text, added tag refresh to click handler
2. **Mirror Actors button**: Added `.HAlign(HAlign_Center)` to center text
3. **Class Picker section**: Removed "Class Picker Filter" title, changed checkbox text from "Enable Class Picker Filter" to "Enable Class Filter"

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp`

**Status**: ✅ COMPLETE

---
### UI Polish: Toolbar and Tag UX Improvements

**Changes**:
1. **Toolbar dropdown menu**: Added "Select in volume: " prefix to all menu items for clarity
   - "All Actor Types" → "Select in volume: All Actor Types"
   - "Static Mesh Actors" → "Select in volume: Static Mesh Actors"
   - etc. (all 10 items)
   
2. **Tag dropdowns**: Improved UX by auto-adding tags on selection
   - Tags now add immediately when selected from dropdown (no need to click Add button)
   - Removed "Add" buttons for both Include and Exclude tags
   - Moved "Clear" buttons to where "Add" buttons were (right next to dropdowns)
   - Cleaner, faster workflow

3. **Fixed LogToolMenus errors**: Removed redundant Tools menu entries
   - Eliminated "UI command not found" errors for SelectActorsInVolumeAll, SelectActorsInVolumeBlueprints, SelectActorsInVolumeLights
   - Toolbar dropdown already provides all functionality

**Files Modified**:
- `Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp` (toolbar text, removed Tools menu)
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp` (tag dropdown auto-add)

**Status**: ✅ COMPLETE

---
### Bug Fix: Toolbar Selection Not Working for Certain Actor Types

**Problem**: Toolbar dropdown menu items for Blueprints, Lights, Decals, Niagara, Cameras, and Blueprints with Lights were not selecting actors even when they were inside the volume.

**Root Cause Analysis**:
1. **Inconsistent implementation**: Only 3 actor types (All, Blueprints, Lights) had dedicated member functions. Other types used empty placeholder lambdas.
2. **Scope issue**: `ESelectActorsInVolumeFilter` enum and `SelectActorsInVolumeInternal` function were defined in anonymous namespace **after** `RegisterMenus()`, causing compilation errors when lambdas tried to reference them.
3. **Invalid bounds handling**: Actors without valid bounding boxes (lights, cameras, decals, niagara) were being skipped entirely instead of falling back to pivot location check.
4. **Overly strict bounds checking**: Used "ContainsFully" logic requiring both Min and Max corners inside volume, which failed for most actors.

**Solution**:
1. **Unified all actor types** to use `SelectActorsInVolumeInternal` with filter enum
2. **Expanded enum** to include all 11 actor types: All, StaticMeshes, Blueprints, Lights, SkeletalMeshes, Decals, Niagara, Volumes, Audio, Cameras, BlueprintsWithLights
3. **Implemented `PassesFilter` function** with proper type checking for all cases
4. **Moved namespace block** before `RegisterMenus()` to fix scope issues
5. **Added fallback for invalid bounds**: Check actor pivot location when `ActorBounds.IsValid` is false
6. **Changed to Intersects logic**: `VolumeBounds.Intersect(ActorBounds)` instead of strict ContainsFully check

**Debug Logging Added**:
``cpp
UE_LOG(LogTemp, Warning, TEXT("SelectActorsInVolumeInternal - Filter: %d, Checked: %d, Passed Filter: %d, Selected: %d"), 
    (int32)Filter, DebugActorsChecked, DebugActorsPassedFilter, DebugActorsSelected);
``

**Files Modified**:
- `Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp` (moved namespace, unified all toolbar menu items, fixed bounds checking)

**Status**:  RESOLVED

---

## February 4, 2026 - Mirror Lights (Blueprints) Y-Axis Fix

### Issue: Blueprint Lights Pointing Wrong Direction After Y-Axis Mirror
**Problem**: When mirroring a Blueprint actor on the Y axis (negative X scale on the actor), light components inside the Blueprint would point down or face the wrong direction. Multiple Euler-based fixes (Pitch/Yaw/Roll combinations) were unstable due to Unreal’s negative scale reinterpretation.

**Root Cause**: Applying negative scale on the parent actor causes Unreal to reinterpret child component rotations. Directly adjusting Euler angles on light components leads to inconsistent results and “flip-flop” behavior.

**Final Solution (WORKING)**: Mirror light components in **world space** and then recompute their **relative transform** after the actor is mirrored. This avoids Euler guesswork and produces correct results for X, Y, and Z axes consistently.

**Implementation Summary**:
- Capture each light component’s original **world transform** before mirroring.
- Mirror the light’s world **location** and **orientation vectors** (Forward/Right/Up) across selected axes.
- Rebuild the mirrored world rotation via `FRotationMatrix::MakeFromXZ(Forward, Up)`.
- Convert mirrored world transform back into a **relative transform** with `GetRelativeTransform(ActorWorldTransform)`.
- Apply to the light component with `SetRelativeTransform()`.

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp`

**Status**: ✅ RESOLVED (confirmed by user)

---

### Bug Fix: Tag Dropdown Re-selection Issue

**Problem**: After clicking Clear button, selecting the same tag again from dropdown wouldn't add it back to the list.

**Root Cause**: `OnSelectionChanged` callback doesn't fire when selecting an item that's already selected.

**Solution**: Clear combo box selection with `SetSelectedItem(nullptr)` immediately after adding tag, allowing same tag to be re-selected.

**Files Modified**:
- `Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp` (both Include and Exclude tag dropdowns)

**Status**:  RESOLVED

---
