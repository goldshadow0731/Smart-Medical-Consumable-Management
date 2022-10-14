import datetime
import os

from dotenv import load_dotenv
from flask import Flask
from flask_restx import Api, Namespace, Resource, fields
from flask_session import Session
from flask_login import UserMixin, login_user, LoginManager, logout_user
from pymongo import MongoClient
from werkzeug.security import  check_password_hash


# Load ENV
load_dotenv()

# Time Setting
tz_delta = datetime.timedelta(hours=8)
tz = datetime.timezone(tz_delta)


app = Flask(__name__)

# Session Setting
app.config['SESSION_TYPE'] = os.environ.get('SESSION_TYPE')
app.config['SECRET_KEY'] = os.environ.get('SECRET_KEY')

Session(app)

api = Api(app, version='1.0.0',
          title='Andes api management', doc='/api/doc')

# Namespace
api_ns = Namespace("account", description="帳號管理")
api_test = Namespace("test", description="新增測試")

api.add_namespace(api_ns)
api.add_namespace(api_test)

# Login Manager
login_manager = LoginManager(app)
login_manager.init_app(app)


# Setup Mongodb info
mongodb = MongoClient(
    f'{os.environ.get("SERVER_PROTOCOL")}://{os.environ.get("MONGO_USER")}:{os.environ.get("MONGO_PASSWORD")}@{os.environ.get("SERVER")}')[os.environ.get("DATABASE")]
user_information = mongodb['User_Information']
inventory_data = mongodb['Inventory_Data']


# Model
base_output_payload = api.model('基本輸出', {
    'status': fields.String(required=True, default=0),
    'message': fields.String(required=True, default="")
})


# Module
class User(UserMixin):
    pass


@login_manager.user_loader
def load_user(user_id):
    user_data = user_information.find_one({"account": user_id})
    if user_data is None:
        return None
    user = User()
    user.account = user_data['account']
    return user


@app.route("/")
def hello():
    return "Hello World!"


@api_ns.route('/login')
class Login(Resource):
    account_login_input_payload = api.model('註冊帳號input', {
        'account': fields.String(required=True, example="admin"),
        'password': fields.String(required=True, example="admin")
    })
    account_login_output_payload = api.clone('註冊帳號output', base_output_payload)

    @api_ns.expect(account_login_input_payload)
    @api_ns.marshal_with(account_login_output_payload)
    def post(self):
        data = api.payload
        account = data.get('account', '')
        user_data = user_information.find_one({"account": account})

        if (user_data is not None) and (check_password_hash(user_data.get('password'), data.get('password'))):
            user = User()
            user.id = user_data['_id']
            login_user(user)
            return {
                "status": 0,
                "message": "Login successfully!"}
        else:
            return {
                "status": 1,
                "message": "Account or password is wrong!"}


@api_ns.route('/logout')
class Logout(Resource):
    def post(self):
        logout_user()
        return {
            "message": "Logged out"}


@api_ns.route('/inventory')
class Inventory(Resource):
    inventory_input_payload = api.model('醫療耗材庫存查詢input', {
        'cabinet': fields.String(required=True, example="A")
    })

    @api_ns.expect(inventory_input_payload)
    def post(self):
        data = api.payload
        cabinet = data.get('cabinet', '')
        inventory = inventory_data.find_one({
            "cabinet": cabinet})
        return inventory['num']


@api_test.route('/insert_data')
class Insert_Data(Resource):
    def post(self):
        try:
            inventory_data.insert_one({
                "cabinet": "B",
                "time": datetime.datetime.now(tz),
                "position": {
                    "cotton swab": {"Row": 1, "Column": 1},
                    "normal saline": {"Row": 1, "Column": 2},
                    "gauze": {"Row": 2, "Column": 1},
                    "ear thermometer": {"Row": 2, "Column": 2}},
                "num": {
                   "cotton swab": 283,
                   "normal saline": 35,
                   "gauze": 72,
                   "ear thermometer": 39
                }
            })
            return "Success"
        except Exception as e:
            print(e)
            return "Fail"


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True, threaded=True)
