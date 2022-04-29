using System.Collections;
using System.Collections.Generic;
using UnityEngine;
#if UNITY_EDITOR
using UnityEditor.Animations;
using UnityEditor;

public class AnimatorSmoother {
    /*[MenuItem("Tools/Naelstrof/ReadAnimation")]
    public static void ReadAnimation() {
        AnimationClip[] animations = Selection.GetFiltered<AnimationClip>(SelectionMode.Assets);
        if (animations.Length != 1) {
            throw new UnityException("Failed: Select a SINGLE animator controller to generate them on!!!!");
        }
        AnimationClip clip = animations[0];
        foreach(var binding in AnimationUtility.GetCurveBindings(clip)) {
            Debug.Log("Path: " + binding.path + "\n" + "Prop: "+binding.propertyName);
        }
    }*/
    [MenuItem("Tools/Naelstrof/Generate AnimatorSmoothers")]
    public static void GenerateAnimatorSmoothers() {
        AnimatorController[] animators = Selection.GetFiltered<AnimatorController>(SelectionMode.Assets);
        if (animators.Length != 1) {
            throw new UnityException("Failed: Select a SINGLE animator controller to generate them on!!!!");
        }
        AnimatorController controller = animators[0];
        foreach(AnimatorControllerParameter parameter in controller.parameters) {
            if (parameter.name.Contains("Smooth")) {
                continue;
            }
            if (parameter.type != AnimatorControllerParameterType.Float) {
                continue;
            }
            if (ContainsSmoothedVersion(parameter,controller)) {
                continue;
            }
            GenerateSmoothed(parameter, controller);
        }
        Debug.Log("Done! Check Assets/ for new animations.");
    }
    static bool ContainsSmoothedVersion(AnimatorControllerParameter sourceParameter, AnimatorController controller) {
        foreach(AnimatorControllerParameter parameter in controller.parameters) {
            if (parameter.name.Contains(sourceParameter.name) && parameter.name.Contains("Smooth")) {
                return true;
            }
        }
        return false;
    }
    static void GenerateSmoothed(AnimatorControllerParameter sourceParameter, AnimatorController controller) {
        string smoothedName = sourceParameter.name+"Smoothed";
        string smoothAmountName = sourceParameter.name+"SmoothAmount";
        controller.AddLayer(sourceParameter.name + "Smoother");
        controller.AddParameter(smoothedName, AnimatorControllerParameterType.Float);
        controller.parameters[controller.parameters.Length-1].defaultFloat = sourceParameter.defaultFloat;
        controller.AddParameter(smoothAmountName, AnimatorControllerParameterType.Float);
        controller.parameters[controller.parameters.Length-1].defaultFloat = 0.1f;
        AnimatorControllerLayer newLayer = controller.layers[controller.layers.Length-1];
        newLayer.defaultWeight = 1f;
        //AnimatorState state = newLayer.stateMachine.AddState(parameter.name+"Smoother");

        AnimationClip zero = GenerateAnimation(smoothedName, 0f);
        AnimationClip one = GenerateAnimation(smoothedName, 1f);

        BlendTree firstBlend;
        controller.CreateBlendTreeInController(sourceParameter.name+"Smoother", out firstBlend, controller.layers.Length-1);
        firstBlend.blendType = BlendTreeType.Simple1D;
        firstBlend.blendParameter = smoothAmountName;
        firstBlend.hideFlags = HideFlags.HideInHierarchy;

        BlendTree currentValue;
        controller.CreateBlendTreeInController("CurrentValue", out currentValue, controller.layers.Length-1);
        currentValue.blendType = BlendTreeType.Simple1D;
        currentValue.blendParameter = smoothedName;
        currentValue.AddChild(zero, 0f);
        currentValue.AddChild(one, 1f);
        currentValue.hideFlags = HideFlags.HideInHierarchy;

        BlendTree wantedValue;
        controller.CreateBlendTreeInController("WantedValue", out wantedValue, controller.layers.Length-1);
        wantedValue.blendType = BlendTreeType.Simple1D;
        wantedValue.blendParameter = sourceParameter.name;
        wantedValue.AddChild(zero, 0f);
        wantedValue.AddChild(one, 1f);
        wantedValue.hideFlags = HideFlags.HideInHierarchy;

        firstBlend.AddChild(currentValue, 0f);
        firstBlend.AddChild(wantedValue, 1f);

        AnimatorState firstState = newLayer.stateMachine.AddState(sourceParameter.name+"Smoother");
        firstState.motion = firstBlend;

        newLayer.stateMachine.defaultState = firstState;

        EditorUtility.SetDirty(controller);
    }

    static AnimationClip GenerateAnimation(string parameter, float amount) {
        AnimationClip clip = new AnimationClip();
        clip.name = parameter+"_"+amount.ToString("F0");
        string dir = "Assets/"+clip.name+".anim";
        AnimationUtility.SetEditorCurve(clip, EditorCurveBinding.FloatCurve("",typeof(Animator), parameter), AnimationCurve.Constant(0f,1f,amount));
        AssetDatabase.CreateAsset(clip,dir);
        return AssetDatabase.LoadAssetAtPath<AnimationClip>(dir);
    }
}
#endif
