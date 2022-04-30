function sign(x)
    if x < 0 then
        return -1
    end
    return 1
end

function update(lipdata)
    -- This compression strategy was developed by [Vilar](https://github.com/Vilar24)! Thank you so much!
    tongueOutStart = lipdata["Tongue_LongStep1"]
    tongueOut = tongueOutStart + lipdata["Tongue_LongStep2"]
    jawOpen = lipdata["Jaw_Open"] - lipdata["Mouth_Ape_Shape"]
    jawOpen = jawOpen*1.3
    jawOpen = math.max(0, jawOpen)
    jawOpen = jawOpen^1.5
    jawOpen = jawOpen-(math.min(lipdata["Cheek_Puff_Left"], lipdata["Cheek_Puff_Right"]) * 1.5)
    mouthXsign = (lipdata["Jaw_Right"] - lipdata["Jaw_Left"])*0.5
    mouthXsign = mouthXsign + ((lipdata["Mouth_Lower_Right"] - lipdata["Mouth_Lower_Left"]) * 0.75)
    mouthXsign = mouthXsign + ((lipdata["Mouth_Upper_Right"] - lipdata["Mouth_Upper_Left"]) * 0.75)
    mouthX = math.abs(lipdata["Jaw_Right"] - lipdata["Jaw_Left"])
    mouthX = math.max(mouthX, math.abs(lipdata["Mouth_Lower_Right"] - lipdata["Mouth_Lower_Left"])) * 1.5
    mouthX = math.max(mouthX, math.abs(lipdata["Mouth_Upper_Right"] - lipdata["Mouth_Upper_Left"])) * 1.5
    mouthX = mouthX*sign(mouthXsign)
    frownPartialA = lipdata["Mouth_Lower_DownLeft"] * lipdata["Mouth_Lower_DownRight"]
    frownPartialB = (lipdata["Mouth_Lower_Overturn"] + lipdata["Mouth_Lower_Overlay"])*0.5
    frown = (lipdata["Mouth_Sad_Left"] + lipdata["Mouth_Sad_Right"]) * 0.5 + frownPartialA * 0.25 + frownPartialB * 0.25

    smileFrown = (lipdata["Mouth_Smile_Left"] + lipdata["Mouth_Smile_Right"]) * 0.5
    smileFrown = smileFrown - (frown * 0.5)
    bareTeeth = math.min(lipdata["Mouth_Upper_UpLeft"], lipdata["Mouth_Lower_DownLeft"])
    bareTeeth = bareTeeth+math.min(lipdata["Mouth_Upper_UpRight"], lipdata["Mouth_Lower_DownRight"])
    poutPartial = (lipdata["Mouth_Upper_Overturn"] * lipdata["Mouth_Lower_Overturn"] *
                   lipdata["Mouth_Upper_UpLeft"] *
                   lipdata["Mouth_Upper_UpRight"] *
                   lipdata["Mouth_Lower_DownLeft"] *
                   lipdata["Mouth_Lower_DownRight"])
    poutReal = lipdata["Mouth_Pout"]*2 + poutPartial * 2
    tongueOut = tongueOut-(poutReal*1.5)
    tongueOut = math.max(tongueOut,0)
    
    tongueUpDown = 0.5 + lipdata["Tongue_Up"]*0.5 - lipdata["Tongue_Down"]*0.5
    tongueRightLeft = 0.5 + lipdata["Tongue_Right"] * 0.5 - lipdata["Tongue_Left"] * 0.5

    SendData("/avatar/parameters/MouthX", (mouthX+1)/2)
    SendData("/avatar/parameters/JawOpen", (jawOpen+1)/2)
    SendData("/avatar/parameters/Smile", (smileFrown+1)/2)
    SendData("/avatar/parameters/Teeth", bareTeeth)
    SendData("/avatar/parameters/Pout", poutReal)
    SendData("/avatar/parameters/Tongue", tongueOut)
    SendData("/avatar/parameters/TongueX", tongueRightLeft)
    SendData("/avatar/parameters/TongueY", tongueUpDown)
end