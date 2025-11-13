ecm_add_qml_module(OpenCloudGui
        URI eu.OpenCloud.gui
        VERSION 1.0
        NAMESPACE OCC
        #QT_NO_PLUGIN
        GENERATE_PLUGIN_SOURCE
        QML_FILES
        OpenCloud.js

        AccountBar.qml
        AccountButton.qml
        FolderDelegate.qml
        FolderError.qml

        UpdateUrlDialog.qml

        credentials/Credentials.qml
        credentials/OAuthCredentials.qml

        spaces/SpaceDelegate.qml
        spaces/SpacesView.qml
)
